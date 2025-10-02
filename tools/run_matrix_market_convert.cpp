#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <graph_zeppelin_common.h>
#include "stream_types.h"


#include <string>
#include <unordered_set>
#include <vector>
#include <random>
#include <algorithm>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_matrix_market_file> <output_binary_file>" << std::endl;
        return 1;
    }
    std::string in_filename = argv[1];
    std::string out_filename = argv[2];
    
    std::cout << "Input Filename: " << in_filename << std::endl;
    std::cout << "Output Filename: " << out_filename << std::endl;
    
    std::ifstream infile(in_filename);
    if (!infile.is_open()) {
        std::cerr << "Error: Could not open input file " << in_filename << std::endl;
        return 1;
    }

    std::ofstream outfile(out_filename, std::ios::binary);
    if (!outfile.is_open()) {
        std::cerr << "Error: Could not open output file " << out_filename << std::endl;
        return 1;
    }
    // read a matrix market file header:
    std::string line;
    std::getline(infile, line);

    // Check for banner
    if (line.find("%%MatrixMarket") == std::string::npos) {
        std::cerr << "Error: Invalid Matrix Market file. Missing banner." << std::endl;
        return 1;
    }

    // Parse header line
    std::stringstream header_ss(line);
    std::string banner, object, format, field, symmetry;
    header_ss >> banner >> object >> format >> field >> symmetry;

    std::cout << "  - Object: " << object << std::endl;
    std::cout << "  - Format: " << format << std::endl;
    std::cout << "  - Field: " << field << std::endl;
    std::cout << "  - Symmetry: " << symmetry << std::endl;

    // Skip comments
    while (std::getline(infile, line) && line[0] == '%') {
        // a comment line, skip
    }

    // Read dimensions
    std::stringstream dim_ss(line);
    int rows, cols, nonzeros;
    if (format == "coordinate") {
        dim_ss >> rows >> cols >> nonzeros;
        std::cout << "  - Dimensions: " << rows << "x" << cols << " with " << nonzeros << " non-zero entries." << std::endl;
    } else if (format == "array") {
        dim_ss >> rows >> cols;
        std::cout << "  - Dimensions: " << rows << "x" << cols << std::endl;
    } else {
        std::cerr << "Error: Unsupported format: " << format << std::endl;
        return 1;
    }
    node_id_t num_vertices = std::max(rows, cols);
    
    std::unordered_set<edge_id_t> existing_edges;
    edge_id_t edge_count = 0;

    // Read entries
    size_t line_count = 0;
    while (std::getline(infile, line)) {
        line_count++;
        if (line_count % 10000000 == 0) {
            std::cout << "  - Processed " << line_count << " / "<< (format == "coordinate" ? nonzeros : (rows * cols)) << " lines." << std::endl;
        }
        if (line.empty() || line[0] == '%') continue; // skip empty
        std::stringstream entry_ss(line);
        node_id_t r, c;
        double value = 1.0; // default for pattern
        if (format == "coordinate") {
            entry_ss >> r >> c;
            if (field != "pattern") {
                entry_ss >> value;
            }
        } else if (format == "array") {
            entry_ss >> value;
            r = (edge_count % rows) + 1;
            c = (edge_count / rows) + 1;
        }
        // needs to be 0 based:
        r -= 1;
        c -= 1;
        node_id_t src = std::min(r, c);
        node_id_t dst = std::max(r, c);
        edge_id_t encoded = (edge_id_t(src) << 32U) | edge_id_t(dst);
        if (existing_edges.find(encoded) == existing_edges.end()) {
            existing_edges.insert(encoded);
            edge_count++;
        }
    }
        
    std::cout << "  - Read " << edge_count << " unique edges." << std::endl;
    infile.close();
    // write edges to a vector:
    std::vector<edge_id_t> shuffled_edges(existing_edges.begin(), existing_edges.end());
    // shuffle edges:
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(shuffled_edges.begin(), shuffled_edges.end(), g);
    
    std::cout << "  - Shuffled edges." << std::endl;
    
    // write to binary file:
    outfile.write(reinterpret_cast<const char*>(&num_vertices), sizeof(node_id_t));
    outfile.write(reinterpret_cast<const char*>(&edge_count), sizeof(edge_id_t));
    GraphStreamUpdate update_buffer[1024];
    size_t num_buffered_items = 0;
    size_t edges_written = 0;
    for (const auto& encoded : shuffled_edges) {
        node_id_t src = static_cast<node_id_t>(encoded >> 32U);
        node_id_t dst = static_cast<node_id_t>(encoded & 0xFFFFFFFFU);
        uint8_t type = INSERT;
        Edge edge{src, dst};
        GraphStreamUpdate update{type, edge};
        update_buffer[num_buffered_items++] = update;
        if (num_buffered_items == 1024) {
            outfile.write(reinterpret_cast<const char*>(update_buffer), sizeof(GraphStreamUpdate) * num_buffered_items);
            edges_written += num_buffered_items;
            num_buffered_items = 0;
            if (edges_written % 10000000 <= 1024)
            {
                std::cout << "  - Written " << edges_written << " / " << edge_count << " edges." << std::endl;
            }
        }
        // outfile.write(reinterpret_cast<const char*>(&update), sizeof(GraphStreamUpdate));
    }
    if (num_buffered_items > 0) {
        outfile.write(reinterpret_cast<const char*>(update_buffer), sizeof(GraphStreamUpdate) * num_buffered_items);
        edges_written += num_buffered_items;
    }
    std::cout << "  - Written to binary file." << std::endl;
    outfile.close();

    return 0;
};