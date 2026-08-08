#include <Arduino.h>
#include <FastLED.h>

// Налаштування матриці
#define DATA_PIN 6
#define MATRIX_WIDTH 16
#define MATRIX_HEIGHT 16
#define NUM_LEDS (MATRIX_WIDTH * MATRIX_HEIGHT)
#define BRIGHTNESS 20

// Піни керування
#define X_PIN A1
#define Y_PIN A2
#define SW_PIN 8

CRGB leds[NUM_LEDS];
CRGB board[MATRIX_WIDTH][MATRIX_HEIGHT];

// Таймери
unsigned long last_time_x = 0;
unsigned long last_time_rotation = 0;
unsigned long lastFallTime = 0;
unsigned long gameOverTime = 0;

unsigned long fallInterval = 600;          // Базова швидкість падіння (мс)
const unsigned long softDropInterval = 60; // Швидкість при утриманні джойстика вниз

// Фігури Тетріса (4x4)
const byte tetrominoes[7][4][4] = {
    // I Piece
    {{0, 0, 0, 0},
     {1, 1, 1, 1},
     {0, 0, 0, 0},
     {0, 0, 0, 0}},
    // T Piece
    {{0, 1, 0, 0},
     {1, 1, 1, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0}},
    // O Piece
    {{1, 1, 0, 0},
     {1, 1, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0}},
    // J Piece
    {{1, 0, 0, 0},
     {1, 1, 1, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0}},
    // L Piece
    {{0, 0, 1, 0},
     {1, 1, 1, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0}},
    // S Piece
    {{0, 1, 1, 0},
     {1, 1, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0}},
    // Z Piece
    {{1, 1, 0, 0},
     {0, 1, 1, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0}}};

const CRGB colors[7] = {
    CRGB::Cyan, CRGB::Purple, CRGB::Yellow, CRGB::Blue, CRGB::Orange, CRGB::Green, CRGB::Red};

// Стан активної фігури
short currentPiece, currentX, currentY, currentRotation;
bool gameOver = false;
bool redrawNeeded = true;

// Перетворення координат (X, Y) у 1D індекс (Зігзаг / Serpentine)
uint16_t getLEDIndex(short x, short y)
{
  if (x < 0 || x >= MATRIX_WIDTH || y < 0 || y >= MATRIX_HEIGHT)
    return 0;

  if (y % 2 == 0)
  {
    return (y * MATRIX_WIDTH) + x;
  }
  else
  {
    return (y * MATRIX_WIDTH) + (MATRIX_WIDTH - 1 - x);
  }
}

// Перевірка колізій
bool checkCollision(short piece, short rotation, short posX, short posY)
{
  for (short r = 0; r < 4; r++)
  {
    for (short c = 0; c < 4; c++)
    {
      short rotX = r, rotY = c;
      if (rotation == 1)
      {
        rotX = 3 - c;
        rotY = r;
      }
      else if (rotation == 2)
      {
        rotX = 3 - r;
        rotY = 3 - c;
      }
      else if (rotation == 3)
      {
        rotX = c;
        rotY = 3 - r;
      }

      if (tetrominoes[piece][rotY][rotX])
      {
        short targetX = posX + c;
        short targetY = posY + r;

        if (targetX < 0 || targetX >= MATRIX_WIDTH || targetY >= MATRIX_HEIGHT)
        {
          return true; // Вихід за межі стін або підлоги
        }
        if (targetY >= 0 && board[targetX][targetY] != CRGB(0, 0, 0))
        {
          return true; // Зіткнення з іншим блоком
        }
      }
    }
  }
  return false;
}

// Спавн нової фігури
void spawnPiece()
{
  currentPiece = random(0, 7);
  currentRotation = 0;
  currentX = MATRIX_WIDTH / 2 - 2;
  currentY = 0;

  if (checkCollision(currentPiece, currentRotation, currentX, currentY))
  {
    gameOver = true;
    gameOverTime = millis();
  }
}

// Фіксація фігури на сітці
void lockPiece()
{
  for (short r = 0; r < 4; r++)
  {
    for (short c = 0; c < 4; c++)
    {
      short rotX = r, rotY = c;
      if (currentRotation == 1)
      {
        rotX = 3 - c;
        rotY = r;
      }
      else if (currentRotation == 2)
      {
        rotX = 3 - r;
        rotY = 3 - c;
      }
      else if (currentRotation == 3)
      {
        rotX = c;
        rotY = 3 - r;
      }

      if (tetrominoes[currentPiece][rotY][rotX])
      {
        short targetX = currentX + c;
        short targetY = currentY + r;
        if (targetY >= 0 && targetX >= 0 && targetX < MATRIX_WIDTH)
        {
          board[targetX][targetY] = colors[currentPiece];
        }
      }
    }
  }
}

// Очищення заповнених ліній
void clearLines()
{
  for (short y = MATRIX_HEIGHT - 1; y >= 0; y--)
  {
    bool rowFull = true;
    for (short x = 0; x < MATRIX_WIDTH; x++)
    {
      if (board[x][y] == CRGB(0, 0, 0))
      {
        rowFull = false;
        break;
      }
    }
    if (rowFull)
    {
      for (short moveY = y; moveY > 0; moveY--)
      {
        for (short x = 0; x < MATRIX_WIDTH; x++)
        {
          board[x][moveY] = board[x][moveY - 1];
        }
      }
      for (short x = 0; x < MATRIX_WIDTH; x++)
      {
        board[x][0] = CRGB(0, 0, 0);
      }
      y++; // Переперевірка поточного рядка
    }
  }
}

// Відображення кадру
void renderFrame()
{
  FastLED.clear();

  if (gameOver)
  {
    fill_solid(leds, NUM_LEDS, CRGB::Red);
  }
  else
  {
    // 1. Омалювання нерухомих блоків
    for (short x = 0; x < MATRIX_WIDTH; x++)
    {
      for (short y = 0; y < MATRIX_HEIGHT; y++)
      {
        if (board[x][y] != CRGB(0, 0, 0))
        {
          leds[getLEDIndex(x, y)] = board[x][y];
        }
      }
    }

    // 2. Омалювання падаючої фігури
    for (short r = 0; r < 4; r++)
    {
      for (short c = 0; c < 4; c++)
      {
        short rotX = r, rotY = c;
        if (currentRotation == 1)
        {
          rotX = 3 - c;
          rotY = r;
        }
        else if (currentRotation == 2)
        {
          rotX = 3 - r;
          rotY = 3 - c;
        }
        else if (currentRotation == 3)
        {
          rotX = c;
          rotY = 3 - r;
        }

        if (tetrominoes[currentPiece][rotY][rotX])
        {
          short targetX = currentX + c;
          short targetY = currentY + r;
          if (targetY >= 0 && targetY < MATRIX_HEIGHT && targetX >= 0 && targetX < MATRIX_WIDTH)
          {
            leds[getLEDIndex(targetX, targetY)] = colors[currentPiece];
          }
        }
      }
    }
  }

  FastLED.show();
}

void setup()
{
  // Підтяжка для кнопки
  pinMode(SW_PIN, INPUT_PULLUP);

  randomSeed(analogRead(A0));

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);

  memset(board, 0, sizeof(board));
  spawnPiece();
}

void loop()
{
  // 1. Обробка стану Game Over
  if (gameOver)
  {
    if (redrawNeeded)
    {
      renderFrame();
      redrawNeeded = false;
    }

    // Пауза 2 секунди після програшу
    if (millis() - gameOverTime >= 2000)
    {
      memset(board, 0, sizeof(board));
      gameOver = false;
      spawnPiece();
      redrawNeeded = true;
    }
    return;
  }

  short valX = analogRead(X_PIN);
  short valY = analogRead(Y_PIN);

  // 2. Рух ВЛІВО / ВПРАВО (з затримкою антидребезгу 120 мс)
  if (millis() - last_time_x >= 120)
  {
    if (valX < 300) // Вліво
    {
      if (!checkCollision(currentPiece, currentRotation, currentX - 1, currentY))
      {
        currentX--;
        redrawNeeded = true;
      }
      last_time_x = millis();
    }
    else if (valX > 700) // Вправо
    {
      if (!checkCollision(currentPiece, currentRotation, currentX + 1, currentY))
      {
        currentX++;
        redrawNeeded = true;
      }
      last_time_x = millis();
    }
  }

  // 3. ОБЕРТАННЯ КНОПКОЮ (з антидребезгом 200 мс)
  if (digitalRead(SW_PIN) == LOW)
  {
    if (millis() - last_time_rotation >= 200)
    {
      int nextRotation = (currentRotation + 1) % 4;
      if (!checkCollision(currentPiece, nextRotation, currentX, currentY))
      {
        currentRotation = nextRotation;
        redrawNeeded = true;
      }
      last_time_rotation = millis();
    }
  }

  // 4. ПРИСКОРЕНЕ ПАДІННЯ (якщо джойстик відхилено вниз)
  unsigned long currentInterval = (valY > 700) ? softDropInterval : fallInterval;

  // 5. Автоматичне падіння фігури
  if (millis() - lastFallTime >= currentInterval)
  {
    if (!checkCollision(currentPiece, currentRotation, currentX, currentY + 1))
    {
      currentY++;
    }
    else
    {
      lockPiece();
      clearLines();
      spawnPiece();
    }
    lastFallTime = millis();
    redrawNeeded = true;
  }

  // 6. Отрисовка кадру ТІЛЬКИ при зміні стану (захист від лагів)
  if (redrawNeeded)
  {
    renderFrame();
    redrawNeeded = false;
  }
}