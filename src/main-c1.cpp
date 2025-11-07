#include <Arduino.h>

// A Core-1-nek stack külön legyen a Core-0-tól
// https://arduino-pico.readthedocs.io/en/latest/multicore.html#stack-sizes
bool core1_separate_stack = true;

/**
 * @brief Core1 main belépő függvénye
 */
void setup1() {}

/**
 * @brief Core1 fő ciklus függvénye
 */
void loop1() {}
