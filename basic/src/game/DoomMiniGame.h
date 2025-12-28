#pragma once
#include <Arduino.h>
#include <math.h>
#include "drivers/LcdDriver.h"
#include "drivers/ButtonsDriver.h"
#include "core/UiStateService.h"

// Pequeño raycaster estilo Doom adaptado a u8g2/ST7567 y 3 botones.
// Controles:
//  - UP: avanzar
//  - DOWN: rotar derecha
//  - MID: rotar izquierda (long-press 3s/6s para salir al menú)
class DoomMiniGame {
public:
    void begin(LcdDriver* lcdDriver, UiStateService* uiState) {
        lcd = lcdDriver;
        ui  = uiState;
    }

    void start(uint32_t nowMs) {
        if (!lcd || !ui) return;
        posX      = 3.5f;
        posY      = 3.5f;
        dirX      = 1.0f;
        dirY      = 0.0f;
        planeX    = 0.0f;
        planeY    = 0.66f; // FOV ~66°
        running   = true;
        lastFrame = nowMs;
        rotateMode = false;
        centerHeld = false;
        centerRepeatUsed = false;
        pressedUp = pressedMid = pressedDown = false;
        flashFrames = 0;
        spawnEnemies();
    }

    void stop() {
        running = false;
        if (lcd) {
            lcd->getU8g2().clearBuffer();
            lcd->getU8g2().sendBuffer();
        }
    }

    bool isRunning() const { return running; }

    void handleButton(ButtonId id, ButtonEventType type) {
        if (!running) return;

        switch (type) {
        case ButtonEventType::PRESS:
            if (id == ButtonId::MID) {
                // Entra en modo giro; no dispara.
                rotateMode = true;
                centerHeld = true;
                centerRepeatUsed = false;
                pressedMid = true;
            } else if (rotateMode) {
                if (id == ButtonId::UP) {
                    rotate(rotSpeed);
                    pressedUp = true;
                } else if (id == ButtonId::DOWN) {
                    rotate(-rotSpeed);
                    pressedDown = true;
                }
            } else {
                if (id == ButtonId::UP) {
                    moveForward(moveSpeed);
                    pressedUp = true;
                } else if (id == ButtonId::DOWN) {
                    moveForward(-moveSpeed * 0.7f); // retroceso más lento
                    pressedDown = true;
                } else if (id == ButtonId::MID) {
                    useAction();
                    pressedMid = true;
                }
            }
            break;

        case ButtonEventType::REPEAT:
            // Repeats permiten giro continuo o avance continuo.
            if (rotateMode) {
                if (id == ButtonId::UP) {
                    rotate(rotSpeed * 0.6f);
                    pressedUp = true;
                } else if (id == ButtonId::DOWN) {
                    rotate(-rotSpeed * 0.6f);
                    pressedDown = true;
                } else if (id == ButtonId::MID && !centerRepeatUsed) {
                    useAction(); // “usar” mientras mantienes
                    centerRepeatUsed = true;
                    pressedMid = true;
                }
            } else {
                if (id == ButtonId::UP) {
                    moveForward(moveSpeed * 0.9f);
                    pressedUp = true;
                } else if (id == ButtonId::DOWN) {
                    moveForward(-moveSpeed * 0.7f);
                    pressedDown = true;
                }
            }
            break;

        case ButtonEventType::RELEASE:
            if (id == ButtonId::MID) {
                rotateMode = false;
                centerHeld = false;
                centerRepeatUsed = false;
                pressedMid = false;
            } else if (id == ButtonId::UP) {
                pressedUp = false;
            } else if (id == ButtonId::DOWN) {
                pressedDown = false;
            }
            break;

        default:
            break;
        }

        // Salida segura: los tres botones a la vez
        if (pressedUp && pressedMid && pressedDown) {
            stop();
            ui->setScreen(UiScreen::MENU_ROOT);
        }
    }

    void update(uint32_t nowMs) {
        if (!running || !lcd) return;

        // Render sin usar delta compleja; se siente fluido a ~30-50 FPS.
        U8G2& u8 = lcd->getU8g2();
        u8.clearBuffer();

        drawBackground(u8);
        castRays(u8);
        renderEnemies(u8);
        drawFlash(u8);

        u8.sendBuffer();
        lastFrame = nowMs;
    }

private:
    // --- Configuración del mini-juego ---
    static constexpr uint8_t SCREEN_W = 128;
    static constexpr uint8_t SCREEN_H = 64;
    static constexpr uint8_t RENDER_H = 56; // deja parte baja para HUD
    static constexpr uint8_t RAY_STEP = 2;  // densidad de rayos (2px)

    static constexpr float moveSpeed = 0.14f;
    static constexpr float rotSpeed  = 0.18f;

    // Mapa simple 12x12 (1 = muro, 0 = libre)
    static constexpr uint8_t MAP_W = 24;
    static constexpr uint8_t MAP_H = 24;
    static const char kMap[MAP_H][MAP_W + 1] PROGMEM;
    static constexpr uint8_t MAX_ENEMIES = 10;

    LcdDriver*      lcd = nullptr;
    UiStateService* ui  = nullptr;
    bool            running = false;
    uint32_t        lastFrame = 0;
    bool            rotateMode = false;
    bool            centerHeld = false;
    bool            centerRepeatUsed = false;
    bool            pressedUp = false;
    bool            pressedMid = false;
    bool            pressedDown = false;
    uint8_t         flashFrames = 0;
    float           zbuf[SCREEN_W]{};
    struct Enemy {
        float x;
        float y;
        bool  alive;
        uint8_t flash;
    };
    Enemy enemies[MAX_ENEMIES]{};

    float posX = 3.5f;
    float posY = 3.5f;
    float dirX = 1.0f;
    float dirY = 0.0f;
    float planeX = 0.0f;
    float planeY = 0.66f;

    static bool isWall(int x, int y) {
        if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return true;
        char c = pgm_read_byte(&(kMap[y][x]));
        return c == '1';
    }

    void moveForward(float step) {
        float newX = posX + dirX * step;
        float newY = posY + dirY * step;
        if (!isWall((int)newX, (int)posY)) posX = newX;
        if (!isWall((int)posX, (int)newY)) posY = newY;
    }

    void rotate(float angle) {
        float cosA = cosf(angle);
        float sinA = sinf(angle);

        float oldDirX = dirX;
        dirX = dirX * cosA - dirY * sinA;
        dirY = oldDirX * sinA + dirY * cosA;

        float oldPlaneX = planeX;
        planeX = planeX * cosA - planeY * sinA;
        planeY = oldPlaneX * sinA + planeY * cosA;
    }

    void drawBackground(U8G2& u8) {
        // Cielo (negro) y suelo (línea simple)
        u8.drawHLine(0, RENDER_H, SCREEN_W);
    }

    void castRays(U8G2& u8) {
        for (int x = 0; x < SCREEN_W; x += RAY_STEP) {
            float cameraX = 2.0f * x / SCREEN_W - 1.0f;
            float rayDirX = dirX + planeX * cameraX;
            float rayDirY = dirY + planeY * cameraX;

            int mapX = (int)posX;
            int mapY = (int)posY;

            float deltaDistX = (rayDirX == 0) ? 1e6f : fabsf(1.0f / rayDirX);
            float deltaDistY = (rayDirY == 0) ? 1e6f : fabsf(1.0f / rayDirY);

            float sideDistX;
            float sideDistY;
            int   stepX;
            int   stepY;

            if (rayDirX < 0) {
                stepX = -1;
                sideDistX = (posX - mapX) * deltaDistX;
            } else {
                stepX = 1;
                sideDistX = (mapX + 1.0f - posX) * deltaDistX;
            }

            if (rayDirY < 0) {
                stepY = -1;
                sideDistY = (posY - mapY) * deltaDistY;
            } else {
                stepY = 1;
                sideDistY = (mapY + 1.0f - posY) * deltaDistY;
            }

            bool hit = false;
            bool side = false;
            int depth = 0;
            const int MAX_DEPTH = 32;

            while (!hit && depth < MAX_DEPTH) {
                if (sideDistX < sideDistY) {
                    sideDistX += deltaDistX;
                    mapX += stepX;
                    side = false;
                } else {
                    sideDistY += deltaDistY;
                    mapY += stepY;
                    side = true;
                }

                if (isWall(mapX, mapY)) {
                    hit = true;
                }
                depth++;
            }

            float perpWallDist = side ? (sideDistY - deltaDistY) : (sideDistX - deltaDistX);
            if (perpWallDist < 0.001f) perpWallDist = 0.001f;

            int lineHeight = (int)(RENDER_H / perpWallDist);
            int drawStart  = -lineHeight / 2 + RENDER_H / 2;
            int drawEnd    = lineHeight / 2 + RENDER_H / 2;

            if (drawStart < 0) drawStart = 0;
            if (drawEnd >= RENDER_H) drawEnd = RENDER_H - 1;

            for (int dx = 0; dx < RAY_STEP && (x + dx) < SCREEN_W; ++dx) {
                zbuf[x + dx] = perpWallDist;
            }

            // Sombreado simple: si golpea "lado" dibujar puntos alternos.
            for (int dx = 0; dx < RAY_STEP && (x + dx) < SCREEN_W; ++dx) {
                if (!side) {
                    u8.drawVLine(x + dx, drawStart, drawEnd - drawStart + 1);
                } else {
                    // patrón punteado para simular sombra
                    for (int y = drawStart; y <= drawEnd; y += 2) {
                        u8.drawPixel(x + dx, y);
                    }
                }
            }
        }
    }

    void drawFlash(U8G2& u8) {
        if (flashFrames == 0) return;

        // Pequeño destello en el centro inferior
        uint8_t cx = SCREEN_W / 2;
        uint8_t cy = RENDER_H - 6;
        u8.drawBox(cx - 3, cy - 3, 7, 7);
        flashFrames--;
    }

    void useAction() {
        // Disparo: destello + buscar enemigo en cono frontal
        flashFrames = 5;
        float wallDist = castWallDistance();

        float bestDist = wallDist;
        int   bestIdx  = -1;

        for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
            if (!enemies[i].alive) continue;
            float dx = enemies[i].x - posX;
            float dy = enemies[i].y - posY;
            float dist = sqrtf(dx*dx + dy*dy);
            if (dist <= 0.1f || dist > wallDist) continue;

            float forward = dx * dirX + dy * dirY;
            if (forward <= 0) continue; // detrás

            float perp = fabsf(dx * (-dirY) + dy * dirX);
            if (perp > 0.5f * dist) continue; // fuera de cono

            if (dist < bestDist) {
                bestDist = dist;
                bestIdx = i;
            }
        }

        if (bestIdx >= 0) {
            enemies[bestIdx].alive = false;
        }
    }

    float castWallDistance() {
        float rayDirX = dirX;
        float rayDirY = dirY;
        int mapX = (int)posX;
        int mapY = (int)posY;

        float deltaDistX = (rayDirX == 0) ? 1e6f : fabsf(1.0f / rayDirX);
        float deltaDistY = (rayDirY == 0) ? 1e6f : fabsf(1.0f / rayDirY);

        float sideDistX;
        float sideDistY;
        int   stepX;
        int   stepY;

        if (rayDirX < 0) {
            stepX = -1;
            sideDistX = (posX - mapX) * deltaDistX;
        } else {
            stepX = 1;
            sideDistX = (mapX + 1.0f - posX) * deltaDistX;
        }

        if (rayDirY < 0) {
            stepY = -1;
            sideDistY = (posY - mapY) * deltaDistY;
        } else {
            stepY = 1;
            sideDistY = (mapY + 1.0f - posY) * deltaDistY;
        }

        bool hit = false;
        bool side = false;
        int depth = 0;
        const int MAX_DEPTH = 64;
        while (!hit && depth < MAX_DEPTH) {
            if (sideDistX < sideDistY) {
                sideDistX += deltaDistX;
                mapX += stepX;
                side = false;
            } else {
                sideDistY += deltaDistY;
                mapY += stepY;
                side = true;
            }
            if (isWall(mapX, mapY)) hit = true;
            depth++;
        }
        float dist = side ? (sideDistY - deltaDistY) : (sideDistX - deltaDistX);
        if (dist < 0.001f) dist = 0.001f;
        return dist;
    }

    void renderEnemies(U8G2& u8) {
        for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
            if (!enemies[i].alive) continue;

            float relX = enemies[i].x - posX;
            float relY = enemies[i].y - posY;

            float invDet = 1.0f / (planeX * dirY - dirX * planeY);
            float transX = invDet * (dirY * relX - dirX * relY);
            float transY = invDet * (-planeY * relX + planeX * relY);

            if (transY <= 0.1f || transY > 20.0f) continue;

            int spriteScreenX = (int)(SCREEN_W / 2 * (1 + transX / transY));
            int spriteHeight  = (int)(RENDER_H / transY);
            int spriteWidth   = spriteHeight / 2;

            int drawStartY = -spriteHeight / 2 + RENDER_H / 2;
            int drawEndY   = spriteHeight / 2 + RENDER_H / 2;
            int drawStartX = spriteScreenX - spriteWidth / 2;
            int drawEndX   = spriteScreenX + spriteWidth / 2;

            if (drawStartY < 0) drawStartY = 0;
            if (drawEndY >= RENDER_H) drawEndY = RENDER_H - 1;

            for (int x = drawStartX; x <= drawEndX; ++x) {
                if (x < 0 || x >= SCREEN_W) continue;
                if (zbuf[x] < transY) continue;
                u8.drawVLine(x, drawStartY, drawEndY - drawStartY + 1);
            }
        }
    }

    void spawnEnemies() {
        const float positions[][2] = {
            {5.5f, 5.5f},
            {8.5f, 7.5f},
            {12.5f, 6.5f},
            {15.5f, 10.5f},
            {18.5f, 12.5f},
            {20.5f, 16.5f},
            {6.5f, 14.5f},
            {10.5f, 18.5f},
            {14.5f, 20.5f},
            {9.5f, 22.5f}
        };
        for (uint8_t i = 0; i < MAX_ENEMIES; ++i) {
            enemies[i].x = positions[i][0];
            enemies[i].y = positions[i][1];
            enemies[i].alive = true;
            enemies[i].flash = 0;
        }
    }
};

// Definición del mapa (fuera de la clase para PROGMEM) – clonado simplificado de E1M1.
inline constexpr char DoomMiniGame::kMap[MAP_H][MAP_W + 1] PROGMEM = {
    "111111111111111111111111",
    "100000000011111111110001",
    "101111110011111111110001",
    "101000010000000000010001",
    "101011010011111110010001",
    "101000010010000010010001",
    "101110011010111010010001",
    "100010000010101010010001",
    "101010111010101010010001",
    "101010000010001010010001",
    "101011111111101010010001",
    "101000000000001010010001",
    "101111111111101010010001",
    "101000000000001010010001",
    "101011111110111010010001",
    "101000000010001010010001",
    "101011111010101010010001",
    "101000001010101010010001",
    "101110001010101010010001",
    "100010001010101010010001",
    "101010001010101010010001",
    "101010001000000010010001",
    "100000001011111110000001",
    "111111111111111111111111"
};
