#pragma once

/// @file dds_types.hpp
/// @brief The one place this module includes `api/dds_data_types.hpp`.
///
/// `api/dds_data_types.hpp` declares its own "simple card representation",
/// also named `struct Card` (see its own doxygen) -- unrelated to, and in
/// the same global namespace as, this module's own `struct Card`
/// (belief_evaluation/types.hpp), used throughout this module's whole
/// public surface. The moment both are visible in one translation unit that
/// is a hard redefinition error, regardless of whether either `Card` is
/// ever referenced. Neither is this module's to rename: the solver's is
/// load-bearing well beyond this module, and this module's is load-bearing
/// throughout its own public API.
///
/// The fix is to rename the incoming one, locally, for the duration of this
/// one include -- and to do that in exactly one place rather than at every
/// site that needs a DDS struct, so the rename can't silently stop applying
/// depending on which file a translation unit happens to include first.
/// `#pragma once` on `dds_data_types.hpp` means its content is only ever
/// pasted in on the *first* inclusion in a given translation unit; as long
/// as every belief_evaluation file that needs `Deal`, `FutureTricks`, and
/// friends includes this wrapper instead of `api/dds_data_types.hpp`
/// directly, that first inclusion is always this one, and the rename always
/// takes effect -- irrespective of include order between belief_evaluation
/// files themselves.
#define Card DdsInternalCard
#include <api/dds_data_types.hpp>
#undef Card
