#include <belief_evaluation/default_defender_heuristics.hpp>

#include <api/dds_constants.hpp>

#include <belief_evaluation/discard_keep_winners.hpp>
#include <belief_evaluation/fourth_seat_low.hpp>
#include <belief_evaluation/high_in_third.hpp>
#include <belief_evaluation/ruff_small.hpp>
#include <belief_evaluation/second_seat_low.hpp>
#include <belief_evaluation/third_seat_low.hpp>

namespace dds::belief_evaluation
{

auto make_default_defender_heuristics(int trump, bool randomize_touching_honors) -> DefenderHeuristicChain
{
    DefenderHeuristicChain chain;
    chain.add(second_seat_low(randomize_touching_honors));
    chain.add(high_in_third());
    chain.add(third_seat_low());
    chain.add(fourth_seat_low());
    if (trump != DDS_NOTRUMP)
    {
        chain.add(ruff_small());
    }
    chain.add(discard_keep_winners());
    return chain;
}

}  // namespace dds::belief_evaluation
