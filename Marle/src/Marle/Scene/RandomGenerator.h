#pragma once

#include <cstdint>

namespace Marle {

    // A random number generator whose results are the same on every platform and standard library,
    // for behaviors and anything else that must be deterministic (ADR-0002). It is PCG32 (O'Neill,
    // pcg-random.org, the minimal version with its reference seeding), and the helpers below are ours:
    // the standard library's distributions are not used because their results differ between
    // implementations. The same seed (and stream) always gives the same sequence, with nothing taken
    // from the clock or the machine.
    class RandomGenerator
    {
        public:
            static constexpr uint64_t DefaultStream = 54;   // the stream of the reference demo

            // Everything the generator remembers; saving and restoring it replays the same numbers.
            struct State {
                uint64_t Value = 0;
                uint64_t Increment = 0;
                bool operator==(const State& other) const { return Value == other.Value && Increment == other.Increment; }
                bool operator!=(const State& other) const { return !(*this == other); }
            };

            explicit RandomGenerator(uint64_t seed = 1, uint64_t stream = DefaultStream) { Seed(seed, stream); }

            // Restarts the sequence. Different seeds, and different streams, give unrelated sequences.
            void Seed(uint64_t seed, uint64_t stream = DefaultStream)
            {
                m_State = 0;
                m_Increment = (stream << 1u) | 1u;
                NextU32();
                m_State += seed;
                NextU32();
            }

            // 32 uniformly distributed bits
            uint32_t NextU32()
            {
                const uint64_t old = m_State;
                m_State = old * 6364136223846793005ULL + m_Increment;
                const uint32_t xorShifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
                const uint32_t rotation = (uint32_t)(old >> 59u);
                return (xorShifted >> rotation) | (xorShifted << ((0u - rotation) & 31u));
            }

            // A uniform integer from 0 to bound - 1, without modulo bias; 0 if bound is 0
            uint32_t Below(uint32_t bound)
            {
                if (bound == 0) return 0;
                const uint32_t threshold = (0u - bound) % bound;   // the results below this would favor small values
                for (;;) {
                    const uint32_t value = NextU32();
                    if (value >= threshold) return value % bound;
                }
            }

            // A uniform integer from min to max, both included; min if max is smaller
            int IntBetween(int min, int max)
            {
                if (max < min) return min;
                const uint64_t span = (uint64_t)((int64_t)max - (int64_t)min) + 1u;
                if (span > 0xFFFFFFFFull) return (int)NextU32();   // the whole range of int
                return (int)((int64_t)min + (int64_t)Below((uint32_t)span));
            }

            // A uniform float from 0 up to but not including 1, from 24 random bits so every value is exact
            float NextFloat() { return (float)(NextU32() >> 8u) * (1.0f / 16777216.0f); }

            // A uniform float from min up to but not including max (min if max is not larger)
            float FloatBetween(float min, float max) { return max > min ? min + NextFloat() * (max - min) : min; }

            // True with the given probability: never at 0 or less, always at 1 or more
            bool Chance(float probability) { return NextFloat() < probability; }

            State GetState() const { return { m_State, m_Increment }; }
            void SetState(const State& state) { m_State = state.Value; m_Increment = state.Increment; }

        private:
            uint64_t m_State = 0;
            uint64_t m_Increment = 0;
    };

}
