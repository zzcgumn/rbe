#include <belief_evaluation/renumber.hpp>

auto renumber(unsigned holding, unsigned pool) -> unsigned
{
    unsigned result = 0;
    unsigned out_bit = 1;
    for (unsigned in_bit = 1; pool != 0 && in_bit != 0; in_bit <<= 1)
    {
        if ((pool & in_bit) != 0)
        {
            if ((holding & in_bit) != 0)
            {
                result |= out_bit;
            }
            out_bit <<= 1;
            pool &= ~in_bit;
        }
    }
    return result;
}
