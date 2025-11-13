#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include <graph_zeppelin_common.h>

#include "stream_types.h"

namespace {
constexpr size_t kBufferCapacity = 1 << 15; 

inline uint64_t make_edge_key(node_id_t u, node_id_t v) {
    return (static_cast<uint64_t>(u) << 32) | static_cast<uint64_t>(v);
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                            << " <input_binary_file> <output_binary_file> <dense_threshold>" << std::endl;
        return 1;
    }

    const std::string in_filename = argv[1];
    const std::string out_filename = argv[2];

    size_t dense_threshold = 0;
    try {
        dense_threshold = std::stoul(argv[3]);
    } catch (const std::exception& err) {
        std::cerr << "Invalid dense threshold: " << err.what() << std::endl;
        return 1;
    }

    std::ifstream in_file(in_filename, std::ios::binary);
    if (!in_file) {
        std::cerr << "Error opening input file: " << in_filename << std::endl;
        return 1;
    }

    std::ofstream out_file(out_filename, std::ios::binary | std::ios::trunc);
    if (!out_file) {
        std::cerr << "Error opening output file: " << out_filename << std::endl;
        return 1;
    }

    node_id_t num_vertices = 0;
    edge_id_t declared_updates = 0;
    in_file.read(reinterpret_cast<char*>(&num_vertices), sizeof(node_id_t));
    in_file.read(reinterpret_cast<char*>(&declared_updates), sizeof(edge_id_t));
    if (!in_file) {
        std::cerr << "Error reading stream header from: " << in_filename << std::endl;
        return 1;
    }

    std::cout << "Input vertices: " << num_vertices << ", Declared updates: "
                        << declared_updates << std::endl;

    std::vector<size_t> degrees(num_vertices, 0);
    std::unordered_set<uint64_t> active_edges;
    active_edges.reserve(static_cast<size_t>(declared_updates));

    std::vector<GraphStreamUpdate> buffer(kBufferCapacity);

    size_t processed_updates = 0;
    size_t applied_inserts = 0;
    size_t applied_deletes = 0;
    size_t skipped_self_loops = 0;
    size_t skipped_out_of_range = 0;
    bool hit_breakpoint = false;

    while (!hit_breakpoint && in_file) {
        in_file.read(reinterpret_cast<char*>(buffer.data()),
                                 static_cast<std::streamsize>(buffer.size() * sizeof(GraphStreamUpdate)));
        std::streamsize bytes_read = in_file.gcount();
        if (bytes_read == 0) {
            break;
        }

        if (bytes_read % static_cast<std::streamsize>(sizeof(GraphStreamUpdate)) != 0) {
            std::cerr << "Input stream contains a partial update record." << std::endl;
            return 1;
        }

        const size_t updates_read = static_cast<size_t>(bytes_read / sizeof(GraphStreamUpdate));
        for (size_t i = 0; i < updates_read; ++i) {
            const GraphStreamUpdate& update = buffer[i];
            ++processed_updates;

            const UpdateType type = static_cast<UpdateType>(update.type);
            if (type == BREAKPOINT) {
                hit_breakpoint = true;
                break;
            }

            const node_id_t src_raw = update.edge.src;
            const node_id_t dst_raw = update.edge.dst;

            if (src_raw >= num_vertices || dst_raw >= num_vertices) {
                ++skipped_out_of_range;
                continue;
            }

            if (src_raw == dst_raw) {
                ++skipped_self_loops;
                continue;
            }

                    node_id_t u = src_raw;
                    node_id_t v = dst_raw;
                    if (u > v) {
                        std::swap(u,v);
                    }
            const uint64_t key = make_edge_key(u, v);

            if (type == INSERT) {
                const auto insert_result = active_edges.insert(key);
                if (insert_result.second) {
                    ++applied_inserts;
                    ++degrees[u];
                    ++degrees[v];
                }
            } else if (type == DELETE) {
                const auto it = active_edges.find(key);
                if (it != active_edges.end()) {
                    active_edges.erase(it);
                    ++applied_deletes;
                    if (degrees[u] > 0) --degrees[u];
                    if (degrees[v] > 0) --degrees[v];
                }
            } else {
                std::cerr << "Encountered unknown update type: " << static_cast<int>(update.type)
                                    << std::endl;
                return 1;
            }
        }
    }

    if (!hit_breakpoint && !in_file.eof() && in_file.fail()) {
        std::cerr << "Error while reading updates from: " << in_filename << std::endl;
        return 1;
    }

    std::cout << "Processed updates: " << processed_updates << std::endl;
    std::cout << "Applied inserts: " << applied_inserts << ", Applied deletes: " << applied_deletes
                        << std::endl;
    std::cout << "Skipped self loops: " << skipped_self_loops
                        << ", Skipped out-of-range edges: " << skipped_out_of_range << std::endl;

    std::vector<bool> dense_mask(num_vertices, false);
    size_t dense_vertex_count = 0;
    for (node_id_t vertex = 0; vertex < num_vertices; ++vertex) {
        if (degrees[vertex] >= dense_threshold) {
            dense_mask[vertex] = true;
            ++dense_vertex_count;
        }
    }

    edge_id_t dense_edge_count = 0;
    for (const uint64_t key : active_edges) {
        const node_id_t u = static_cast<node_id_t>(key >> 32U);
        const node_id_t v = static_cast<node_id_t>(key & 0xFFFFFFFFU);
        if (dense_mask[u] && dense_mask[v]) {
            ++dense_edge_count;
        }
    }

    std::cout << "Vertices with degree >= " << dense_threshold << ": " << dense_vertex_count
                        << std::endl;
    std::cout << "Number of output edges: " << dense_edge_count << std::endl;

    out_file.write(reinterpret_cast<const char*>(&num_vertices), sizeof(node_id_t));
    out_file.write(reinterpret_cast<const char*>(&dense_edge_count), sizeof(edge_id_t));
    if (!out_file) {
        std::cerr << "Error writing stream header to: " << out_filename << std::endl;
        return 1;
    }

    std::vector<GraphStreamUpdate> out_buffer;
    out_buffer.reserve(kBufferCapacity);

    edge_id_t emitted_edges = 0;
    for (const uint64_t key : active_edges) {
        const node_id_t u = static_cast<node_id_t>(key >> 32U);
        const node_id_t v = static_cast<node_id_t>(key & 0xFFFFFFFFU);
        if (!dense_mask[u] || !dense_mask[v]) {
            continue;
        }

        GraphStreamUpdate update{};
        update.type = INSERT;
        update.edge.src = u;
        update.edge.dst = v;
        out_buffer.push_back(update);
        ++emitted_edges;

        if (out_buffer.size() == out_buffer.capacity()) {
            out_file.write(reinterpret_cast<const char*>(out_buffer.data()),
                                         static_cast<std::streamsize>(out_buffer.size() * sizeof(GraphStreamUpdate)));
            if (!out_file) {
                std::cerr << "Error writing updates to: " << out_filename << std::endl;
                return 1;
            }
            out_buffer.clear();
        }
    }

    if (!out_buffer.empty()) {
        out_file.write(reinterpret_cast<const char*>(out_buffer.data()),
                                     static_cast<std::streamsize>(out_buffer.size() * sizeof(GraphStreamUpdate)));
        if (!out_file) {
            std::cerr << "Error writing updates to: " << out_filename << std::endl;
            return 1;
        }
    }

    if (emitted_edges != dense_edge_count) {
        std::cerr << "Warning: wrote " << emitted_edges
                            << " edges but expected " << dense_edge_count << std::endl;
    }

    return 0;
}

