#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <esp_partition.h>

// Backend de bitácora robusto: ring buffer con headers A/B + CRC.
// Soporta cortes de energía gracias a commit en dos fases y headers duplicados.

// Debug
#ifndef LOGBOOK_DEBUG
#define LOGBOOK_DEBUG 1
#endif
#if LOGBOOK_DEBUG
  #define LB_DBG(...)  do{ Serial.printf(__VA_ARGS__); }while(0)
#else
  #define LB_DBG(...)  do{}while(0)
#endif

// Ruta y parámetros
#ifndef LOGBOOK_FILE_PATH
#define LOGBOOK_FILE_PATH   "/logbook.bin"
#endif
#ifndef LOGBOOK_POSIX_PATH
#define LOGBOOK_POSIX_PATH  "/littlefs" LOGBOOK_FILE_PATH
#endif
#ifndef LOGBOOK_HDR_SLOT_SIZE
#define LOGBOOK_HDR_SLOT_SIZE 4096u   // 4 KiB alineado
#endif
#ifndef LOGBOOK_CAPACITY
#define LOGBOOK_CAPACITY    30000u    // nº de saltos
#endif

class LogbookService {
public:
    struct __attribute__((packed)) Record {
        uint32_t id        = 0;
        uint32_t tsUtc     = 0;
        float    exitAltM  = 0.0f;
        float    deployAltM= 0.0f;
        float    freefallTimeS = 0.0f;
        float    vmaxFFmps     = 0.0f;
        float    vmaxCanopymps = 0.0f;
        uint16_t flags     = 0;
        uint16_t crc16     = 0;
    };

    struct Stats {
        uint32_t count    = 0;
        uint32_t totalIds = 0;
        uint32_t capacity = 0;
    };

    bool begin() {
        if (!ensureFS()) {
            Serial.println("[logbook] LittleFS no montó / no abrió.");
            return false;
        }

        LB_DBG("[logbook] schema: sizeof(Record)=%u crcOff=%u\n",
               (unsigned)sizeof(Record), (unsigned)offsetof(Record, crc16));

        if (!loadHeaderAB()) {
            Serial.println("[logbook] Formateando archivo de bitácora...");
            formatFreshFile(LOGBOOK_CAPACITY);
        } else {
            if (hdr.version != LB_HDR_VER || hdr.rec_size != sizeof(Record)) {
                Serial.println("[logbook] Header incompatible → reformateando archivo.");
                formatFreshFile(LOGBOOK_CAPACITY);
            } else {
                reconcileCapacity();
                quickFixTailSlots();
                LB_DBG("[logbook] Header OK: head=%u count=%u nextId=%u gen=%u size=%u\n",
                       (unsigned)hdr.head, (unsigned)hdr.count,
                       (unsigned)hdr.nextId, (unsigned)hdr.gen, (unsigned)posixGetSize());
            }
        }
        return true;
    }

    bool reset() {
        if (!hdrLoaded) { Serial.println("[logbook] append abort: header not loaded"); return false; }
        formatFreshFile(hdr.capacity);
        return true;
    }

    bool append(const Record& rIn) {
        if (!hdrLoaded) return false;

        Record rec = rIn;
        rec.id     = hdr.nextId;
        rec.flags |= FLAG_VALID;
        rec.crc16  = recCrc(rec);

        uint32_t pos = hdr.head % hdr.capacity;
        uint32_t off = dataBaseOffset() + pos * sizeof(Record);

        if (!ensureDataCapacityPOSIX(off + sizeof(Record))) {
            LB_DBG("[logbook] ensure capacity FAIL (off+size=%u)\n", (unsigned)(off + (uint32_t)sizeof(Record)));
            return false;
        }

        // Commit en 2 fases
        Record tmp = rec;
        tmp.flags &= ~FLAG_VALID;
        tmp.crc16  = recCrc(tmp);
        bool ok = false;
        for (int att = 0; att < 2 && !ok; ++att) {
            ok = posixWriteAt(off, &tmp, sizeof(tmp));
            if (!ok) delay(5);
        }
        if (!ok) {
            LB_DBG("[logbook] ERROR posixWriteAt(off=0x%X sizeNow=%u)\n", (unsigned)off, (unsigned)posixGetSize());
            return false;
        }

        uint16_t flags2 = rec.flags;
        uint16_t crc2;
        {
            Record tmp2 = rec; tmp2.flags = flags2; tmp2.crc16 = 0;
            crc2 = recCrc(tmp2);
        }
        if (!posixWriteAt(off + (uint32_t)offsetof(Record, flags), &flags2, sizeof(flags2))) return false;
        if (!posixWriteAt(off + (uint32_t)offsetof(Record, crc16), &crc2, sizeof(crc2))) return false;

        hdr.head = (pos + 1) % hdr.capacity;
        if (hdr.count < hdr.capacity) hdr.count++;
        hdr.nextId++;
        hdr.gen++;
        bool hdrOk = storeHeaderAB();
        LB_DBG("[logbook] append %s id=%lu pos=%u count=%u next=%u gen=%u fileSize=%u\n",
               hdrOk?"ok":"FAIL",
               (unsigned long)rec.id, (unsigned)pos,
               (unsigned)hdr.count, (unsigned)hdr.nextId, (unsigned)hdr.gen,
               (unsigned)posixGetSize());
        if (!hdrOk) {
            Serial.println("[logbook] append failed writing header");
        }
        return hdrOk;
    }

    bool getStats(Stats& st) const {
        if (!hdrLoaded) return false;
        st.count    = hdr.count;
        st.totalIds = (hdr.nextId > 0) ? (hdr.nextId - 1) : 0;
        st.capacity = hdr.capacity;
        return true;
    }

    bool getByIndex(uint16_t idxNewestFirst, Record& out) {
        if (!hdrLoaded || hdr.count == 0) return false;
        if (idxNewestFirst >= hdr.count) return false;

        uint32_t last = (hdr.head == 0) ? (hdr.capacity - 1) : (hdr.head - 1);
        uint32_t pos  = (last + hdr.capacity - idxNewestFirst) % hdr.capacity;
        uint32_t off  = dataBaseOffset() + pos * sizeof(Record);

        if (!posixReadAt(off, &out, sizeof(out))) {
            LB_DBG("[logbook] ERROR readAt(off=0x%X)\n", (unsigned)off);
            return false;
        }

        uint16_t expect = recCrc(out);
        if (expect != out.crc16) {
            LB_DBG("[logbook] CRC BAD en pos=%u (got=0x%04X exp=0x%04X)\n",
                   (unsigned)pos, out.crc16, expect);
            return false;
        }
        if (!(out.flags & FLAG_VALID)) {
            LB_DBG("[logbook] registro no comprometido en pos=%u\n", (unsigned)pos);
            return false;
        }
        return true;
    }

private:
    struct __attribute__((packed)) Header {
        uint32_t magic    = 0x4C4F4742; // "LOGB"
        uint16_t version  = 1;
        uint16_t rec_size = sizeof(Record);
        uint32_t capacity = LOGBOOK_CAPACITY;
        uint32_t head     = 0;
        uint32_t count    = 0;
        uint32_t nextId   = 1;
        uint32_t gen      = 1;
        uint16_t crc      = 0;
    };

    static constexpr uint16_t FLAG_VALID  = 0x0001;
    static constexpr uint16_t LB_HDR_VER  = 1;
    static constexpr uint32_t LB_MAGIC    = 0x4C4F4742; // "LOGB"

    static uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < len; ++i) {
            crc ^= (uint16_t)data[i] << 8;
            for (int b = 0; b < 8; ++b) crc = (crc & 0x8000) ? ((crc<<1) ^ 0x1021) : (crc<<1);
        }
        return crc;
    }

    static uint16_t hdrCrc(const Header& h) {
        Header tmp = h;
        tmp.crc = 0;
        return crc16_ccitt(reinterpret_cast<const uint8_t*>(&tmp), sizeof(tmp));
    }

    static uint16_t recCrc(const Record& r) {
        Record tmp = r;
        tmp.crc16 = 0;
        return crc16_ccitt(reinterpret_cast<const uint8_t*>(&tmp), sizeof(tmp));
    }

    static uint32_t dataBaseOffset() { return (uint32_t)LOGBOOK_HDR_SLOT_SIZE * 2u; }

    bool ensureFS() {
        if (fsMounted) return true;
        printFSPartitionInfo();
        // Mantener consistente con el montaje global (basePath=/littlefs, label=spiffs)
        if (LittleFS.begin(false, "/littlefs", 5, "spiffs")) {
            fsMounted = true; LB_DBG("[logbook] LittleFS montado.\n"); return true;
        }
        LB_DBG("[logbook] LittleFS.begin(false) falló. Formateando...\n");
        fsMounted = false;
        LittleFS.end(); delay(50);
        if (!LittleFS.format()) { LB_DBG("[logbook] LittleFS.format() falló.\n"); return false; }
        if (!LittleFS.begin(false, "/littlefs", 5, "spiffs")) { LB_DBG("[logbook] LittleFS.begin tras format falló.\n"); return false; }
        fsMounted = true; LB_DBG("[logbook] LittleFS formateado y montado.\n"); return true;
    }

    void printFSPartitionInfo() {
        const esp_partition_t* p =
            esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
        if (p) {
            LB_DBG("[logbook] Partición '%s' addr=0x%X size=%u bytes\n",
                   p->label, (unsigned)p->address, (unsigned)p->size);
        } else {
            LB_DBG("[logbook] ¡Partición 'spiffs' no encontrada!\n");
        }
    }

    uint32_t posixGetSize() const {
        struct stat st;
        if (::stat(LOGBOOK_POSIX_PATH, &st) == 0) return (uint32_t)st.st_size;
        return 0u;
    }

    int openRWfd_with_retry() {
        for (int att = 0; att < 2; ++att) {
            int fd = ::open(LOGBOOK_POSIX_PATH, O_RDWR | O_CREAT, 0666);
            if (fd >= 0) return fd;
            int e = errno;
            LB_DBG("[logbook] open(O_RDWR) FAIL (errno=%d %s)\n", e, strerror(e));
            if (e == EIO) {
                fsMounted = false;
                LittleFS.end();
                delay(10);
                if (!LittleFS.begin(false, "/littlefs", 5, "spiffs")) {
                    LB_DBG("[logbook] Remontaje LittleFS tras EIO falló\n");
                    continue;
                }
                fsMounted = true;
                LB_DBG("[logbook] LittleFS remontado; reintentando open()\n");
            } else {
                break;
            }
        }
        return -1;
    }

    bool posixWriteAt(uint32_t off, const void* buf, size_t len) {
        if (!ensureFS()) return false;
        int fd = openRWfd_with_retry();
        if (fd < 0) return false;
        if (::lseek(fd, (off_t)off, SEEK_SET) < 0) {
            LB_DBG("[logbook] lseek FAIL (errno=%d %s)\n", errno, strerror(errno));
            ::close(fd); return false;
        }
        ssize_t wr = ::write(fd, buf, len);
        if (wr != (ssize_t)len) {
            LB_DBG("[logbook] write FAIL (off=0x%X wr=%d len=%u errno=%d %s)\n",
                   (unsigned)off, (int)wr, (unsigned)len, errno, strerror(errno));
            ::close(fd);
            return false;
        }
        ::fsync(fd);
        ::close(fd);
        return true;
    }

    bool posixReadAt(uint32_t off, void* buf, size_t len) const {
        if (len == 0) return true;
        int fd = ::open(LOGBOOK_POSIX_PATH, O_RDONLY);
        if (fd < 0) { LB_DBG("[logbook] open(O_RDONLY) FAIL (errno=%d %s)\n", errno, strerror(errno)); return false; }
        if (::lseek(fd, (off_t)off, SEEK_SET) < 0) {
            LB_DBG("[logbook] lseek(READ) FAIL (errno=%d %s)\n", errno, strerror(errno));
            ::close(fd); return false;
        }
        ssize_t rd = ::read(fd, buf, len);
        ::close(fd);
        if (rd != (ssize_t)len) {
            LB_DBG("[logbook] read FAIL (off=0x%X len=%u rd=%d)\n", (unsigned)off, (unsigned)len, (int)rd);
            return false;
        }
        return true;
    }

    bool posixExtendTo(uint32_t targetSize) {
        if (!ensureFS()) return false;
        uint32_t cur = posixGetSize();
        if (cur >= targetSize) return true;

        int fd = openRWfd_with_retry();
        if (fd < 0) return false;

        if (::lseek(fd, 0, SEEK_END) < 0) {
            LB_DBG("[logbook] lseek END FAIL (errno=%d %s)\n", errno, strerror(errno));
            ::close(fd); return false;
        }
        static uint8_t zeros[1024];
        memset(zeros, 0, sizeof(zeros));

        while (cur < targetSize) {
            uint32_t need  = targetSize - cur;
            uint32_t chunk = (need > sizeof(zeros)) ? sizeof(zeros) : need;
            ssize_t wr = ::write(fd, zeros, chunk);
            if (wr <= 0) {
                LB_DBG("[logbook] append extend FAIL (cur=%u target=%u wr=%d errno=%d %s)\n",
                       (unsigned)cur, (unsigned)targetSize, (int)wr, errno, strerror(errno));
                ::close(fd);
                return false;
            }
            cur += (uint32_t)wr;
        }
        ::fsync(fd);
        ::close(fd);
        LB_DBG("[logbook] Archivo extendido (POSIX) a %u bytes\n", (unsigned)targetSize);
        return true;
    }

    bool ensureDataCapacityPOSIX(uint32_t needSize) {
        uint32_t cur = posixGetSize();
        if (cur >= needSize) return true;
        return posixExtendTo(needSize);
    }

    bool readHeaderSlot(uint32_t off, Header& out) {
        Header tmp{};
        if (!posixReadAt(off, &tmp, sizeof(tmp))) return false;
        if (tmp.magic    != LB_MAGIC)     return false;
        if (tmp.version  != LB_HDR_VER)   return false;
        if (tmp.rec_size != sizeof(Record)) return false;
        if (tmp.capacity == 0)            return false;
        if (tmp.crc      != hdrCrc(tmp)) return false;
        out = tmp; return true;
    }

    bool writeHeaderSlot(uint32_t off, const Header& h) {
        if (!ensureDataCapacityPOSIX(off + (uint32_t)sizeof(h))) return false;
        for (int att = 0; att < 2; ++att) {
            if (posixWriteAt(off, &h, sizeof(h))) return true;
            delay(5);
        }
        return false;
    }

    bool loadHeaderAB() {
        Header A{}, B{};
        bool okA = readHeaderSlot(0, A);
        bool okB = readHeaderSlot(LOGBOOK_HDR_SLOT_SIZE, B);
        if (!okA && !okB) { hdrLoaded = false; return false; }
        hdr = (okA && okB) ? ((A.gen >= B.gen) ? A : B) : (okA ? A : B);
        hdrLoaded = true;
        return true;
    }

    bool storeHeaderAB() {
        hdr.crc = hdrCrc(hdr);
        bool okB = writeHeaderSlot(LOGBOOK_HDR_SLOT_SIZE, hdr); // B primero
        bool okA = writeHeaderSlot(0, hdr);                     // luego A
        if (!okA || !okB) LB_DBG("[logbook] ERROR al escribir headers A/B (okA=%d okB=%d)\n", okA, okB);
        return okA && okB;
    }

    void formatFreshFile(uint32_t capacity) {
        // truncar
        File fw = LittleFS.open(LOGBOOK_FILE_PATH, "w");
        if (!fw) { LB_DBG("[logbook] NO se pudo truncar/crear con 'w'\n"); }
        fw.close();

        memset(&hdr, 0, sizeof(hdr));
        hdr.magic    = LB_MAGIC;
        hdr.version  = LB_HDR_VER;
        hdr.rec_size = sizeof(Record);
        hdr.capacity = capacity;
        hdr.head     = 0;
        hdr.count    = 0;
        hdr.nextId   = 1;
        hdr.gen      = 1;
        hdr.crc      = hdrCrc(hdr);

        (void)ensureDataCapacityPOSIX(dataBaseOffset());
        (void)storeHeaderAB();
        hdrLoaded = true;
        LB_DBG("[logbook] Archivo nuevo: cap=%u rec=%u bytes base=0x%X size=%u\n",
               (unsigned)hdr.capacity, (unsigned)hdr.rec_size,
               (unsigned)dataBaseOffset(), (unsigned)posixGetSize());
    }

    void reconcileCapacity() {
        if (hdr.capacity == LOGBOOK_CAPACITY) {
            (void)ensureDataCapacityPOSIX(dataBaseOffset());
            return;
        }
        uint32_t oldCap = hdr.capacity;
        uint32_t newCap = LOGBOOK_CAPACITY;
        if (newCap > oldCap) {
            uint32_t need = dataBaseOffset();
            if (ensureDataCapacityPOSIX(need)) {
                hdr.capacity = newCap;
                hdr.gen++;
                writeBothHeaders();
                LB_DBG("[logbook] Capacidad ampliada old=%u -> new=%u\n", (unsigned)oldCap, (unsigned)newCap);
            } else {
                LB_DBG("[logbook] ERROR expandiendo archivo; se mantiene capacidad=%u\n", (unsigned)oldCap);
            }
        } else {
            Serial.println("[logbook] Capacidad menor solicitada → reformateando archivo.");
            formatFreshFile(newCap);
        }
    }

    void writeBothHeaders() { (void)storeHeaderAB(); }

    void quickFixTailSlots(uint32_t maxProbe = 4) {
        if (!hdrLoaded) return;
        if (hdr.count == 0) return;
        uint32_t fixed = 0;
        Record tmp{};
        while (fixed < maxProbe && hdr.count > 0) {
            uint32_t last = (hdr.head == 0) ? (hdr.capacity - 1) : (hdr.head - 1);
            uint32_t off  = dataBaseOffset() + last * sizeof(Record);
            if (!posixReadAt(off, &tmp, sizeof(tmp))) break;
            uint16_t exp = recCrc(tmp);
            if ((tmp.flags & FLAG_VALID) && exp == tmp.crc16) break;
            hdr.head = last;
            hdr.count--;
            fixed++;
        }
        if (fixed) { hdr.gen++; storeHeaderAB(); }
    }

    Header hdr{};
    bool   hdrLoaded = false;
    bool   fsMounted = false;
};
