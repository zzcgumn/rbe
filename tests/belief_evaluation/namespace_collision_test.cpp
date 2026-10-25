#include <gtest/gtest.h>

#include <type_traits>

#include <api/dds.h>

#include <belief_evaluation/types.hpp>

// Includes api/dds.h (global ::Card) and the module header types.hpp
// (dds::belief_evaluation::Card) in one translation unit, with no renaming.
// The test uses both names. If the namespaces merged, or one Card were
// renamed, it would fail instead of compiling for the wrong reason.

TEST(NamespaceCollision, DdsAndModuleCardTypesCoexistAndAreDistinct)
{
    ::Card const solver_card{0, 14};
    dds::belief_evaluation::Card const module_card{0, 14};
    static_assert(!std::is_same_v<decltype(solver_card), decltype(module_card)>);
    EXPECT_EQ(solver_card.suit, module_card.suit);
    EXPECT_EQ(solver_card.rank, module_card.rank);
}
