#pragma once

/// Compress `holding` to consecutive low bits, keeping only the positions
/// set in `pool`. Bit 0 of the result is the lowest outstanding card in the
/// suit. Both arguments use dds's aggregate bit convention: bit i is
/// absolute rank i + 2.
auto renumber(unsigned holding, unsigned pool) -> unsigned;
