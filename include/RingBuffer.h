/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: RingBuffer.h                                                                                                  *
 * Created Date: 2025.11.07.                                                                                           *
 *                                                                                                                     *
 * Author: BT-Soft                                                                                                     *
 * GitHub: https://github.com/bt-soft                                                                                  *
 * Blog: https://electrodiy.blog.hu/                                                                                   *
 * -----                                                                                                               *
 * Copyright (c) 2025 BT-Soft                                                                                          *
 * License: MIT License                                                                                                *
 * 	Bárki szabadon használhatja, módosíthatja, terjeszthet, beépítheti más                                             *
 * 	projektbe (akár zártkódúba is), akár pénzt is kereshet vele                                                        *
 * 	Egyetlen feltétel:                                                                                                 *
 * 		a licencet és a szerző nevét meg kell tartani a forrásban!                                                     *
 * -----                                                                                                               *
 * Last Modified: 2025.11.16, Sunday  09:48:53                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

/**
 * @class RingBuffer
 * @brief Egy egyszerű, sablon alapú ring buffer (cirkuláris puffer).
 *
 * Ez az implementáció egyetlen "producer" (termelő) és egyetlen "consumer" (fogyasztó) szálra/magra van optimalizálva (SPSC).
 * A head és tail indexek atomi változók, hogy elkerüljük a race condition-t a két mag között anélkül, hogy bonyolultabb zárolásra lenne szükség.
 *
 * @tparam T A pufferben tárolt elemek típusa.
 * @tparam Size A puffer mérete (elemekben). Kettő hatványának kell lennie a hatékony indexeléshez.
 */
template <typename T, size_t Size> class RingBuffer {
  public:
    RingBuffer() : head(0), tail(0) {}

    /**
     * @brief Egy elem betétele a pufferbe (producer oldal).
     * @param item A betenni kívánt elem.
     * @return True, ha a betétel sikeres, false, ha a puffer tele van.
     */
    bool put(const T &item) {
        const size_t current_head = head.load(std::memory_order_relaxed);
        const size_t next_head = (current_head + 1) & (Size - 1);

        if (next_head == tail.load(std::memory_order_acquire)) {
            // A puffer tele van
            return false;
        }

        memcpy(&buffer[current_head], &item, sizeof(T));
        head.store(next_head, std::memory_order_release);
        return true;
    }

    /**
     * @brief Egy elem kivétele a pufferből (consumer oldal).
     * @param item A referencia, ahová az elem másolódik.
     * @return True, ha a kivétel sikeres, false, ha a puffer üres.
     */
    bool get(T &item) {
        const size_t current_tail = tail.load(std::memory_order_relaxed);

        if (current_tail == head.load(std::memory_order_acquire)) {
            // A puffer üres
            return false;
        }

        memcpy(&item, &buffer[current_tail], sizeof(T));
        tail.store((current_tail + 1) & (Size - 1), std::memory_order_release);
        return true;
    }

    /**
     * @brief Ellenőrzi, hogy a puffer üres-e.
     */
    bool isEmpty() const { return head.load(std::memory_order_acquire) == tail.load(std::memory_order_acquire); }

    /**
     * @brief Ellenőrzi, hogy a puffer tele van-e.
     */
    bool isFull() const { return ((head.load(std::memory_order_acquire) + 1) & (Size - 1)) == tail.load(std::memory_order_acquire); }

    /**
     * @brief Visszaállítja a puffert üres állapotba.
     */
    void clear() {
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
    }

  private:
    T buffer[Size];
    // Az atomi változók biztosítják a láthatóságot a két mag között
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
};
