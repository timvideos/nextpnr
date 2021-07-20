/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Symbiflow Authors
 *
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

#include "arch.h"
#include "design_utils.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include <boost/algorithm/string.hpp>
#include <queue>

NEXTPNR_NAMESPACE_BEGIN

enum ClusterWireNodeState
{
    IN_SINK_SITE = 0,
    IN_ROUTING = 1,
    IN_SOURCE_SITE = 2,
    ONLY_IN_SOURCE_SITE = 3
};

enum ExpansionDirection
{
    CLUSTER_UPHILL_DIR = 0,
    CLUSTER_DOWNHILL_DIR = 1
};

struct ClusterWireNode
{
    WireId wire;
    ClusterWireNodeState state;
    int depth;
};

static void handle_expansion_node(const Context *ctx, WireId prev_wire, PipId pip, ClusterWireNode curr_node,
                                  std::vector<ClusterWireNode> &nodes_to_expand, pool<BelId> &bels,
                                  ExpansionDirection direction)
{
    WireId wire;

    if (direction == CLUSTER_UPHILL_DIR)
        wire = ctx->getPipSrcWire(pip);
    else
        wire = ctx->getPipDstWire(pip);

    if (wire == WireId())
        return;

    ClusterWireNode next_node;
    next_node.wire = wire;
    next_node.depth = curr_node.depth;

    if (next_node.depth >= 2)
        return;

    auto const &wire_data = ctx->wire_info(wire);

    bool expand_node = true;
    if (ctx->is_site_port(pip)) {
        switch (curr_node.state) {
        case ONLY_IN_SOURCE_SITE:
            expand_node = false;
            break;
        case IN_SOURCE_SITE:
            NPNR_ASSERT(wire_data.site == -1);
            next_node.state = IN_ROUTING;
            break;
        case IN_ROUTING:
            NPNR_ASSERT(wire_data.site != -1);
            next_node.state = IN_SINK_SITE;
            break;
        case IN_SINK_SITE:
            expand_node = false;
            break;
        default:
            // Unreachable!!!
            NPNR_ASSERT(false);
        }
    } else {
        if (next_node.state == IN_ROUTING)
            next_node.depth++;
        next_node.state = curr_node.state;
    }

    if (expand_node)
        nodes_to_expand.push_back(next_node);
    else
        return;

    if (next_node.state == IN_SINK_SITE || next_node.state == ONLY_IN_SOURCE_SITE) {
        for (BelPin bel_pin : ctx->getWireBelPins(wire)) {
            BelId bel = bel_pin.bel;
            auto const &bel_data = bel_info(ctx->chip_info, bel);

            if (bels.count(bel))
                continue;

            if (bel_data.category != BEL_CATEGORY_LOGIC)
                return;

            if (bel_data.synthetic)
                return;

            if (direction == CLUSTER_UPHILL_DIR) {
                // Check that the BEL is indeed the one reached by backward exploration,
                // by checking the previous visited wire.
                for (IdString check_pin : ctx->getBelPins(bel)) {
                    if (prev_wire == ctx->getBelPinWire(bel, check_pin)) {
                        bels.insert(bel);
                        break;
                    }
                }
            } else {
                bels.insert(bel);
            }
        }
    }

    return;
}

static pool<BelId> find_cluster_bels(const Context *ctx, WireId wire, ExpansionDirection direction,
                                     bool out_of_site_expansion = false)
{
    std::vector<ClusterWireNode> nodes_to_expand;
    pool<BelId> bels;

    const auto &wire_data = ctx->wire_info(wire);
    NPNR_ASSERT(wire_data.site != -1);

    ClusterWireNode wire_node;
    wire_node.wire = wire;
    wire_node.state = IN_SOURCE_SITE;
    if (!out_of_site_expansion)
        wire_node.state = ONLY_IN_SOURCE_SITE;
    wire_node.depth = 0;

    nodes_to_expand.push_back(wire_node);

    while (!nodes_to_expand.empty()) {
        ClusterWireNode node_to_expand = nodes_to_expand.back();
        WireId prev_wire = node_to_expand.wire;
        nodes_to_expand.pop_back();

        if (direction == CLUSTER_DOWNHILL_DIR) {
            for (PipId pip : ctx->getPipsDownhill(node_to_expand.wire)) {
                if (ctx->is_pip_synthetic(pip))
                    continue;

                handle_expansion_node(ctx, prev_wire, pip, node_to_expand, nodes_to_expand, bels, direction);
            }
        } else {
            NPNR_ASSERT(direction == CLUSTER_UPHILL_DIR);
            for (PipId pip : ctx->getPipsUphill(node_to_expand.wire)) {
                if (ctx->is_pip_synthetic(pip))
                    continue;

                handle_expansion_node(ctx, prev_wire, pip, node_to_expand, nodes_to_expand, bels, direction);
            }
        }
    }

    return bels;
}

CellInfo *Arch::getClusterRootCell(ClusterId cluster) const
{
    NPNR_ASSERT(cluster != ClusterId());
    return clusters.at(cluster).root;
}

bool Arch::getClusterPlacement(ClusterId cluster, BelId root_bel,
                               std::vector<std::pair<CellInfo *, BelId>> &placement) const
{
    const Context *ctx = getCtx();
    const Cluster &packed_cluster = clusters.at(cluster);

    auto &cluster_data = cluster_info(chip_info, packed_cluster.index);

    CellInfo *root_cell = getClusterRootCell(cluster);
    if (!ctx->isValidBelForCellType(root_cell->type, root_bel))
        return false;

    BelId next_bel;

    // Place cluster
    for (CellInfo *cluster_node : packed_cluster.cluster_nodes) {
        if (cluster_node == root_cell) {
            next_bel = root_bel;
        } else {
            // Find next chained cluster node
            IdString next_bel_pin(cluster_data.chainable_ports[0].bel_source);
            WireId next_bel_pin_wire = ctx->getBelPinWire(next_bel, next_bel_pin);
            next_bel = BelId();
            for (BelId bel :
                 find_cluster_bels(ctx, next_bel_pin_wire, CLUSTER_DOWNHILL_DIR, /*out_of_site_expansion=*/true)) {
                if (ctx->isValidBelForCellType(cluster_node->type, bel)) {
                    next_bel = bel;
                    break;
                }
            }

            if (next_bel == BelId())
                return false;
        }

        // Build a cell to bell mapping required to find BELs connected to the cluster ports.
        dict<IdString, std::vector<IdString>> cell_bel_pins;

        int32_t mapping = bel_info(chip_info, next_bel).pin_map[get_cell_type_index(cluster_node->type)];
        NPNR_ASSERT(mapping >= 0);

        const CellBelMapPOD &cell_pin_map = chip_info->cell_map->cell_bel_map[mapping];
        for (const auto &pin_map : cell_pin_map.common_pins) {
            IdString cell_pin(pin_map.cell_pin);
            IdString bel_pin(pin_map.bel_pin);

            cell_bel_pins[cell_pin].push_back(bel_pin);
        }

        placement.emplace_back(cluster_node, next_bel);

        // Place cluster node cells at the same site
        for (auto port_cell : packed_cluster.cluster_node_cells.at(cluster_node->name)) {
            bool placed_cell = false;

            IdString port = port_cell.first;
            CellInfo *cell = port_cell.second;

            NPNR_ASSERT(cell_bel_pins.count(port));

            PortType port_type = cluster_node->ports.at(port).type;

            if (port_type == PORT_INOUT)
                continue;

            for (auto &bel_pin : cell_bel_pins.at(port)) {
                WireId bel_pin_wire = ctx->getBelPinWire(next_bel, bel_pin);

                ExpansionDirection direction = port_type == PORT_IN ? CLUSTER_UPHILL_DIR : CLUSTER_DOWNHILL_DIR;
                pool<BelId> cluster_bels =
                        find_cluster_bels(ctx, bel_pin_wire, direction, (bool)cluster_data.out_of_site_clusters);

                if (cluster_bels.size() == 0)
                    continue;

                for (BelId bel : cluster_bels) {
                    if (ctx->isValidBelForCellType(cell->type, bel)) {
                        placement.emplace_back(cell, bel);
                        placed_cell = true;
                        break;
                    }
                }

                if (placed_cell)
                    break;
            }

            if (!placed_cell)
                return false;
        }
    }

    return true;
}

ArcBounds Arch::getClusterBounds(ClusterId cluster) const
{
    // TODO: Implement this
    ArcBounds bounds(0, 0, 0, 0);
    return bounds;
}

Loc Arch::getClusterOffset(const CellInfo *cell) const
{
    Loc offset;
    CellInfo *root = getClusterRootCell(cell->cluster);

    if (cell->bel != BelId() && root->bel != BelId()) {
        Loc root_loc = getBelLocation(root->bel);
        Loc cell_loc = getBelLocation(cell->bel);
        offset.x = cell_loc.x - root_loc.x;
        offset.y = cell_loc.y - root_loc.y;
        offset.z = cell_loc.z - root_loc.z;
    } else {
        Cluster cluster = clusters.at(cell->cluster);
        auto &cluster_data = cluster_info(chip_info, cluster.index);

        if (cluster_data.chainable_ports.size() == 0)
            return offset;

        auto &chainable_port = cluster_data.chainable_ports[0];

        IdString cluster_node = cluster.cell_cluster_node_map.at(cell->name);
        CellInfo *cluster_node_cell = cells.at(cluster_node).get();

        auto res = std::find(cluster.cluster_nodes.begin(), cluster.cluster_nodes.end(), cluster_node_cell);
        NPNR_ASSERT(res != cluster.cluster_nodes.end());

        auto distance = std::distance(cluster.cluster_nodes.begin(), res);

        offset.x = chainable_port.avg_x_offset * distance;
        offset.y = chainable_port.avg_y_offset * distance;
    }

    return offset;
}

bool Arch::isClusterStrict(const CellInfo *cell) const { return true; }

static void dump_clusters(const ChipInfoPOD *chip_info, Context *ctx)
{
    for (size_t i = 0; i < chip_info->clusters.size(); ++i) {
        const auto &cluster = chip_info->clusters[i];
        IdString cluster_name(cluster.name);
        log_info("Cluster '%s' loaded! Parameters:\n", cluster_name.c_str(ctx));

        log_info("  - root cell types:\n");
        for (auto cell : cluster.root_cell_types)
            log_info("      - %s\n", IdString(cell).c_str(ctx));

        for (auto chain_ports : cluster.chainable_ports)
            log_info("  - chainable pair: source %s - sink %s\n", IdString(chain_ports.cell_source).c_str(ctx),
                     IdString(chain_ports.cell_sink).c_str(ctx));

        if (cluster.cluster_cells_map.size() != 0)
            log_info("  - cell port maps:\n");
        for (auto cluster_cell : cluster.cluster_cells_map) {
            log_info("    - cell: %s - port: %s\n", IdString(cluster_cell.cell).c_str(ctx),
                     IdString(cluster_cell.port).c_str(ctx));
        }
    }
}

static bool check_cluster_cells_compatibility(CellInfo *old_cell, CellInfo *new_cell, pool<IdString> &exclude_nets)
{
    NPNR_ASSERT(new_cell->type == old_cell->type);
    for (auto &new_port_pair : new_cell->ports) {
        PortInfo new_port_info = new_port_pair.second;
        PortInfo old_port_info = old_cell->ports.at(new_port_pair.first);

        if (exclude_nets.count(new_port_info.net->name))
            continue;

        if (new_port_info.type != PORT_IN)
            continue;

        if (new_port_info.net != old_port_info.net)
            return false;
    }

    return true;
}

void Arch::prepare_cluster(const ClusterPOD *cluster, uint32_t index)
{
    Context *ctx = getCtx();
    IdString cluster_name(cluster->name);

    pool<IdString> cluster_cell_types;
    for (auto cell_type : cluster->root_cell_types)
        cluster_cell_types.insert(IdString(cell_type));

    // Find cluster roots
    std::vector<CellInfo *> roots;
    for (auto &cell : cells) {
        CellInfo *ci = cell.second.get();

        if (ci->cluster != ClusterId())
            continue;

        if (!cluster_cell_types.count(ci->type))
            continue;

        if (cluster->chainable_ports.size() == 0) {
            ci->cluster.set(ctx, ci->name.str(ctx));
            roots.push_back(ci);
            continue;
        }

        // Only one type of dedicated interconnect is allowed.
        auto chain_ports = cluster->chainable_ports[0];
        IdString source_port(chain_ports.cell_source);
        IdString sink_port(chain_ports.cell_sink);

        PortRef driver = ci->ports[sink_port].net->driver;

        if (driver.cell == nullptr || driver.port != source_port) {
            // We hit a root cell
            ci->cluster.set(ctx, ci->name.c_str(ctx));
            roots.push_back(ci);

            // Chained cells use dedicated connections, usually not exposed to the
            // general interconnect resources. The port disconnection is required for
            // sink ports which are connected to GND or VCC by default, which are not
            // reachable due to the fixed dedicated interconnect.
            // E.g.: The CI input of carry chains in 7series corresponds to the CIN bel port,
            //       which can only be connected to the COUT output of the tile below.
            disconnect_port(ctx, ci, sink_port);
        }
    }

    dict<IdString, pool<IdString>> port_cell_maps;
    for (auto cell_port_map : cluster->cluster_cells_map) {
        IdString cell(cell_port_map.cell);
        IdString port(cell_port_map.port);

        pool<IdString> cells_pool({cell});

        port_cell_maps.emplace(port, cells_pool).first->second.insert(cell);
    }

    // Generate unique clusters starting from each root
    for (auto root : roots) {
        Cluster cluster_info;
        cluster_info.root = root;
        cluster_info.index = index;

        CellInfo *next_cluster_node = root;
        if (ctx->verbose)
            log_info("  - forming cluster starting from root cell: %s\n", next_cluster_node->name.c_str(ctx));

        // counter to determine whether this cluster needs to exist
        uint32_t count_cluster_cells = 0;
        do {
            std::vector<std::pair<IdString, CellInfo *>> cluster_cells;

            // type -> cells map to verify compatibility of cells in the same cluster
            dict<IdString, CellInfo *> cell_type_dict;
            pool<IdString> exclude_nets;

            count_cluster_cells++;

            for (auto port : next_cluster_node->ports) {
                if (!port_cell_maps.count(port.first))
                    continue;

                PortInfo port_info = port.second;

                if (port_info.type == PORT_OUT) {
                    exclude_nets.insert(port_info.net->name);
                    auto &users = port_info.net->users;
                    if (users.size() != 1)
                        continue;

                    CellInfo *user_cell = users[0].cell;
                    if (user_cell == nullptr)
                        continue;

                    if (!port_cell_maps.at(port.first).count(user_cell->type))
                        continue;

                    auto res = cell_type_dict.emplace(user_cell->type, user_cell);
                    bool compatible = true;
                    if (!res.second)
                        // Check whether a cell of the same type has all the required nets compatible with
                        // all other nets for the same type. If not, discard the cell.
                        // An example is multiple FFs belonging to the same cluster, where one of them has a different
                        // Set/Reset or CE net w.r.t. the others, making the cluster unplaceable.
                        compatible = check_cluster_cells_compatibility(res.first->second, user_cell, exclude_nets);

                    if (!compatible)
                        continue;

                    user_cell->cluster = root->cluster;
                    cluster_cells.push_back(std::make_pair(port.first, user_cell));
                    cluster_info.cell_cluster_node_map.emplace(user_cell->name, next_cluster_node->name);
                    count_cluster_cells++;

                    if (ctx->verbose)
                        log_info("      - adding user cell: %s\n", user_cell->name.c_str(ctx));

                } else if (port_info.type == PORT_IN) {
                    auto &driver = port_info.net->driver;
                    auto &users = port_info.net->users;
                    if (users.size() != 1)
                        continue;

                    CellInfo *driver_cell = driver.cell;
                    if (driver_cell == nullptr)
                        continue;

                    if (!port_cell_maps.at(port.first).count(driver_cell->type))
                        continue;

                    driver_cell->cluster = root->cluster;
                    cluster_cells.push_back(std::make_pair(port.first, driver_cell));
                    cluster_info.cell_cluster_node_map.emplace(driver_cell->name, next_cluster_node->name);
                    count_cluster_cells++;

                    if (ctx->verbose)
                        log_info("      - adding driver cell: %s\n", driver_cell->name.c_str(ctx));
                }
            }

            cluster_info.cell_cluster_node_map.emplace(next_cluster_node->name, next_cluster_node->name);
            cluster_info.cluster_nodes.push_back(next_cluster_node);
            cluster_info.cluster_node_cells.emplace(next_cluster_node->name, cluster_cells);

            if (cluster->chainable_ports.size() == 0)
                break;

            // Only one type of dedicated interconnect is allowed.
            auto chain_ports = cluster->chainable_ports[0];
            IdString source_port(chain_ports.cell_source);
            IdString sink_port(chain_ports.cell_sink);

            NetInfo *next_net = next_cluster_node->ports.at(source_port).net;

            if (next_net == nullptr)
                continue;

            next_cluster_node = nullptr;
            for (auto &user : next_net->users) {
                CellInfo *user_cell = user.cell;

                if (user_cell == nullptr)
                    continue;

                if (cluster_cell_types.count(user_cell->type)) {
                    user_cell->cluster = root->cluster;
                    next_cluster_node = user_cell;
                    break;
                }
            }

            if (next_cluster_node == nullptr)
                break;

        } while (true);

        if (count_cluster_cells == 1 && cluster->chainable_ports.size() == 0) {
            root->cluster = ClusterId();
            continue;
        }

        clusters.emplace(root->cluster, cluster_info);
    }
}

void Arch::pack_cluster()
{
    Context *ctx = getCtx();

    if (ctx->verbose)
        dump_clusters(chip_info, ctx);

    for (uint32_t i = 0; i < chip_info->clusters.size(); ++i) {
        const auto &cluster = chip_info->clusters[i];

        prepare_cluster(&cluster, i);
    }
}

NEXTPNR_NAMESPACE_END
