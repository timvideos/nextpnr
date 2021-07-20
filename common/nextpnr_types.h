/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@q3k.org>
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

// Types defined in this header use one or more user defined types (e.g. BelId).
// If a new common type is desired that doesn't depend on a user defined type,
// either put it in it's own header, or in nextpnr_base_types.h.
#ifndef NEXTPNR_TYPES_H
#define NEXTPNR_TYPES_H

#include <unordered_map>
#include <unordered_set>

#include "archdefs.h"
#include "hashlib.h"
#include "nextpnr_base_types.h"
#include "nextpnr_namespaces.h"
#include "property.h"

NEXTPNR_NAMESPACE_BEGIN

struct DecalXY
{
    DecalId decal;
    float x = 0, y = 0;

    bool operator==(const DecalXY &other) const { return (decal == other.decal && x == other.x && y == other.y); }
};

struct BelPin
{
    BelId bel;
    IdString pin;
};

struct Region
{
    IdString name;

    bool constr_bels = false;
    bool constr_wires = false;
    bool constr_pips = false;

    pool<BelId> bels;
    pool<WireId> wires;
    pool<Loc> piplocs;
};

struct PipMap
{
    PipId pip = PipId();
    PlaceStrength strength = STRENGTH_NONE;
};

struct CellInfo;

struct PortRef
{
    CellInfo *cell = nullptr;
    IdString port;
    delay_t budget = 0;
};

// minimum and maximum delay
struct DelayPair
{
    DelayPair(){};
    explicit DelayPair(delay_t delay) : min_delay(delay), max_delay(delay){};
    DelayPair(delay_t min_delay, delay_t max_delay) : min_delay(min_delay), max_delay(max_delay){};
    delay_t minDelay() const { return min_delay; };
    delay_t maxDelay() const { return max_delay; };
    delay_t min_delay, max_delay;
    DelayPair operator+(const DelayPair &other) const
    {
        return {min_delay + other.min_delay, max_delay + other.max_delay};
    }
    DelayPair operator-(const DelayPair &other) const
    {
        return {min_delay - other.min_delay, max_delay - other.max_delay};
    }
};

// four-quadrant, min and max rise and fall delay
struct DelayQuad
{
    DelayPair rise, fall;
    DelayQuad(){};
    explicit DelayQuad(delay_t delay) : rise(delay), fall(delay){};
    DelayQuad(delay_t min_delay, delay_t max_delay) : rise(min_delay, max_delay), fall(min_delay, max_delay){};
    DelayQuad(DelayPair rise, DelayPair fall) : rise(rise), fall(fall){};
    DelayQuad(delay_t min_rise, delay_t max_rise, delay_t min_fall, delay_t max_fall)
            : rise(min_rise, max_rise), fall(min_fall, max_fall){};

    delay_t minRiseDelay() const { return rise.minDelay(); };
    delay_t maxRiseDelay() const { return rise.maxDelay(); };
    delay_t minFallDelay() const { return fall.minDelay(); };
    delay_t maxFallDelay() const { return fall.maxDelay(); };
    delay_t minDelay() const { return std::min<delay_t>(rise.minDelay(), fall.minDelay()); };
    delay_t maxDelay() const { return std::max<delay_t>(rise.maxDelay(), fall.maxDelay()); };

    DelayPair delayPair() const { return DelayPair(minDelay(), maxDelay()); };

    DelayQuad operator+(const DelayQuad &other) const { return {rise + other.rise, fall + other.fall}; }
    DelayQuad operator-(const DelayQuad &other) const { return {rise - other.rise, fall - other.fall}; }
};

struct ClockConstraint;

struct NetInfo : ArchNetInfo
{
    IdString name, hierpath;
    int32_t udata = 0;

    PortRef driver;
    std::vector<PortRef> users;
    dict<IdString, Property> attrs;

    // wire -> uphill_pip
    dict<WireId, PipMap> wires;

    std::vector<IdString> aliases; // entries in net_aliases that point to this net

    std::unique_ptr<ClockConstraint> clkconstr;

    Region *region = nullptr;
};

enum PortType
{
    PORT_IN = 0,
    PORT_OUT = 1,
    PORT_INOUT = 2
};

struct PortInfo
{
    IdString name;
    NetInfo *net;
    PortType type;
};

struct CellInfo : ArchCellInfo
{
    IdString name, type, hierpath;
    int32_t udata;

    dict<IdString, PortInfo> ports;
    dict<IdString, Property> attrs, params;

    BelId bel;
    PlaceStrength belStrength = STRENGTH_NONE;

    // cell is part of a cluster if != ClusterId
    ClusterId cluster;

    Region *region = nullptr;

    void addInput(IdString name);
    void addOutput(IdString name);
    void addInout(IdString name);

    void setParam(IdString name, Property value);
    void unsetParam(IdString name);
    void setAttr(IdString name, Property value);
    void unsetAttr(IdString name);
    // check whether a bel complies with the cell's region constraint
    bool testRegion(BelId bel) const;
};

enum TimingPortClass
{
    TMG_CLOCK_INPUT,     // Clock input to a sequential cell
    TMG_GEN_CLOCK,       // Generated clock output (PLL, DCC, etc)
    TMG_REGISTER_INPUT,  // Input to a register, with an associated clock (may also have comb. fanout too)
    TMG_REGISTER_OUTPUT, // Output from a register
    TMG_COMB_INPUT,      // Combinational input, no paths end here
    TMG_COMB_OUTPUT,     // Combinational output, no paths start here
    TMG_STARTPOINT,      // Unclocked primary startpoint, such as an IO cell output
    TMG_ENDPOINT,        // Unclocked primary endpoint, such as an IO cell input
    TMG_IGNORE,          // Asynchronous to all clocks, "don't care", and should be ignored (false path) for analysis
};

enum ClockEdge
{
    RISING_EDGE,
    FALLING_EDGE
};

struct TimingClockingInfo
{
    IdString clock_port; // Port name of clock domain
    ClockEdge edge;
    DelayPair setup, hold; // Input timing checks
    DelayQuad clockToQ;    // Output clock-to-Q time
};

struct ClockConstraint
{
    DelayPair high;
    DelayPair low;
    DelayPair period;
};

// Represents the contents of a non-leaf cell in a design
// with hierarchy

struct HierarchicalPort
{
    IdString name;
    PortType dir;
    std::vector<IdString> nets;
    int offset;
    bool upto;
};

struct HierarchicalCell
{
    IdString name, type, parent, fullpath;
    // Name inside cell instance -> global name
    dict<IdString, IdString> leaf_cells, nets;
    // Global name -> name inside cell instance
    dict<IdString, IdString> leaf_cells_by_gname, nets_by_gname;
    // Cell port to net
    dict<IdString, HierarchicalPort> ports;
    // Name inside cell instance -> global name
    dict<IdString, IdString> hier_cells;
};

NEXTPNR_NAMESPACE_END

#endif /* NEXTPNR_TYPES_H */
