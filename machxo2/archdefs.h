/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xen <claire@symbioticeda.com>
 *  Copyright (C) 2021  William D. Jones <wjones@wdj-consulting.com>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#ifndef MACHXO2_ARCHDEFS_H
#define MACHXO2_ARCHDEFS_H

#include "idstring.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

typedef float delay_t;

// https://bugreports.qt.io/browse/QTBUG-80789

#ifndef Q_MOC_RUN
enum ConstIds
{
    ID_NONE
#define X(t) , ID_##t
#include "constids.inc"
#undef X
    ,
    DB_CONST_ID_COUNT
};

#define X(t) static constexpr auto id_##t = IdString(ID_##t);
#include "constids.inc"
#undef X
#endif

NPNR_PACKED_STRUCT(struct LocationPOD { int16_t x, y; });

struct Location
{
    int16_t x = -1, y = -1;
    Location() : x(-1), y(-1){};
    Location(int16_t x, int16_t y) : x(x), y(y){};
    Location(const LocationPOD &pod) : x(pod.x), y(pod.y){};

    bool operator==(const Location &other) const { return x == other.x && y == other.y; }
    bool operator!=(const Location &other) const { return x != other.x || y != other.y; }
    bool operator<(const Location &other) const { return y == other.y ? x < other.x : y < other.y; }
};

inline Location operator+(const Location &a, const Location &b) { return Location(a.x + b.x, a.y + b.y); }

struct BelId
{
    Location location;
    int32_t index = -1;

    bool operator==(const BelId &other) const { return index == other.index && location == other.location; }
    bool operator!=(const BelId &other) const { return index != other.index || location != other.location; }
    bool operator<(const BelId &other) const
    {
        return location == other.location ? index < other.index : location < other.location;
    }
};

struct WireId
{
    Location location;
    int32_t index = -1;

    bool operator==(const WireId &other) const { return index == other.index && location == other.location; }
    bool operator!=(const WireId &other) const { return index != other.index || location != other.location; }
    bool operator<(const WireId &other) const
    {
        return location == other.location ? index < other.index : location < other.location;
    }
};

struct PipId
{
    Location location;
    int32_t index = -1;

    bool operator==(const PipId &other) const { return index == other.index && location == other.location; }
    bool operator!=(const PipId &other) const { return index != other.index || location != other.location; }
    bool operator<(const PipId &other) const
    {
        return location == other.location ? index < other.index : location < other.location;
    }
};

typedef IdString GroupId;
typedef IdString DecalId;
typedef IdString BelBucketId;

struct ArchNetInfo
{
};

struct NetInfo;

struct ArchCellInfo
{
};

NEXTPNR_NAMESPACE_END

namespace std {
template <> struct hash<NEXTPNR_NAMESPACE_PREFIX Location>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX Location &loc) const noexcept
    {
        std::size_t seed = std::hash<int>()(loc.x);
        seed ^= std::hash<int>()(loc.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX BelId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX BelId &bel) const noexcept
    {
        std::size_t seed = std::hash<NEXTPNR_NAMESPACE_PREFIX Location>()(bel.location);
        seed ^= std::hash<int>()(bel.index) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX WireId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX WireId &wire) const noexcept
    {
        std::size_t seed = std::hash<NEXTPNR_NAMESPACE_PREFIX Location>()(wire.location);
        seed ^= std::hash<int>()(wire.index) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX PipId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX PipId &pip) const noexcept
    {
        std::size_t seed = std::hash<NEXTPNR_NAMESPACE_PREFIX Location>()(pip.location);
        seed ^= std::hash<int>()(pip.index) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

} // namespace std

#endif /* MACHXO2_ARCHDEFS_H */
