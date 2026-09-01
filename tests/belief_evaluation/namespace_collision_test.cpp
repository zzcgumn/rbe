#include <gtest/gtest.h>

#include <type_traits>

#include <api/dds.h>

#include <belief_evaluation/types.hpp>

// A translation unit that includes both api/dds.h (declaring ::Card) and
// this module's own public headers (declaring dds::belief_evaluation::Card)
// directly, with no rename trick anywhere in this file -- the acceptance
// criterion namespacing the module exists to satisfy. Before the module was
// namespaced, this same pair of includes failed with:
//
//   belief_evaluation/types.hpp:24:8: error: redefinition of 'Card'
//   struct Card
//          ^
//   api/dds_data_types.hpp:346:8: note: previous definition is here
//   struct Card
//          ^
//
// References both names below, not just the includes: a future change that
// silently merges the two namespaces, or that renames one Card so it no
// longer shadows the other, would otherwise leave this file compiling for
// the wrong reason.

TEST(NamespaceCollision, DdsAndModuleCardTypesCoexistAndAreDistinct)
{
    ::Card const solver_card{0, 14};
    dds::belief_evaluation::Card const module_card{0, 14};
    static_assert(!std::is_same_v<decltype(solver_card), decltype(module_card)>);
    EXPECT_EQ(solver_card.suit, module_card.suit);
    EXPECT_EQ(solver_card.rank, module_card.rank);
}
