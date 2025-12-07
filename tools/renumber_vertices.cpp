// Renumber vertices in a binary stream based on first appearance order and emit a new stream.
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <graph_zeppelin_common.h>

#include "stream_types.h"

namespace {
constexpr size_t kBufferCapacity = 1 << 15; // 32K updates per chunk for decent IO throughput

// Assign the next available ID the first time we see a vertex.
node_id_t get_or_assign(node_id_t original,
												std::unordered_map<node_id_t, node_id_t>& remap,
												node_id_t& next_id) {
	const auto it = remap.find(original);
	if (it != remap.end()) return it->second;
	const node_id_t assigned = next_id++;
	remap.emplace(original, assigned);
	return assigned;
}
} // namespace

int main(int argc, char* argv[]) {
	if (argc != 3) {
		std::cerr << "Usage: " << argv[0] << " <input_binary_file> <output_binary_file>" << std::endl;
		return 1;
	}

	const std::string in_filename = argv[1];
	const std::string out_filename = argv[2];

	std::ifstream in_file(in_filename, std::ios::binary);
	if (!in_file) {
		std::cerr << "Error opening input file: " << in_filename << std::endl;
		return 1;
	}

	// Use fstream so we can patch the header after writing the body.
	std::fstream out_file(out_filename, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
	if (!out_file) {
		std::cerr << "Error opening output file: " << out_filename << std::endl;
		return 1;
	}

	node_id_t in_num_vertices = 0;
	edge_id_t in_num_updates = 0;
	in_file.read(reinterpret_cast<char*>(&in_num_vertices), sizeof(node_id_t));
	in_file.read(reinterpret_cast<char*>(&in_num_updates), sizeof(edge_id_t));
	if (!in_file) {
		std::cerr << "Error reading stream header from: " << in_filename << std::endl;
		return 1;
	}

	// Placeholder header; will patch after processing.
	node_id_t out_num_vertices = 0;
	edge_id_t out_num_updates = 0;
	out_file.write(reinterpret_cast<const char*>(&out_num_vertices), sizeof(node_id_t));
	out_file.write(reinterpret_cast<const char*>(&out_num_updates), sizeof(edge_id_t));
	if (!out_file) {
		std::cerr << "Error writing placeholder header to: " << out_filename << std::endl;
		return 1;
	}

	std::unordered_map<node_id_t, node_id_t> remap;
	remap.reserve(static_cast<size_t>(in_num_vertices));
	node_id_t next_id = 0;

	std::vector<GraphStreamUpdate> read_buf(kBufferCapacity);
	std::vector<GraphStreamUpdate> write_buf;
	write_buf.reserve(kBufferCapacity);

	bool hit_breakpoint = false;
	while (!hit_breakpoint && in_file) {
		in_file.read(reinterpret_cast<char*>(read_buf.data()),
								 static_cast<std::streamsize>(read_buf.size() * sizeof(GraphStreamUpdate)));
		const std::streamsize bytes_read = in_file.gcount();
		if (bytes_read == 0) break;

		if (bytes_read % static_cast<std::streamsize>(sizeof(GraphStreamUpdate)) != 0) {
			std::cerr << "Input stream contains a partial update record." << std::endl;
			return 1;
		}

		const size_t updates_read = static_cast<size_t>(bytes_read / sizeof(GraphStreamUpdate));
		for (size_t i = 0; i < updates_read; ++i) {
			GraphStreamUpdate upd = read_buf[i];
			const UpdateType type = static_cast<UpdateType>(upd.type);

			if (type == BREAKPOINT) {
				hit_breakpoint = true;
				break;
			}

			if (type != INSERT && type != DELETE) {
				std::cerr << "Encountered unknown update type: " << static_cast<int>(upd.type) << std::endl;
				return 1;
			}

			upd.edge.src = get_or_assign(upd.edge.src, remap, next_id);
			upd.edge.dst = get_or_assign(upd.edge.dst, remap, next_id);

			write_buf.push_back(upd);
			++out_num_updates;

			if (write_buf.size() == write_buf.capacity()) {
				out_file.write(reinterpret_cast<const char*>(write_buf.data()),
											 static_cast<std::streamsize>(write_buf.size() * sizeof(GraphStreamUpdate)));
				if (!out_file) {
					std::cerr << "Error writing updates to: " << out_filename << std::endl;
					return 1;
				}
				write_buf.clear();
			}
		}
	}

	if (!hit_breakpoint && !in_file.eof() && in_file.fail()) {
		std::cerr << "Error while reading updates from: " << in_filename << std::endl;
		return 1;
	}

	if (!write_buf.empty()) {
		out_file.write(reinterpret_cast<const char*>(write_buf.data()),
									 static_cast<std::streamsize>(write_buf.size() * sizeof(GraphStreamUpdate)));
		if (!out_file) {
			std::cerr << "Error writing updates to: " << out_filename << std::endl;
			return 1;
		}
	}

	out_num_vertices = next_id;

	// Patch header with true counts.
	out_file.seekp(0, std::ios::beg);
	out_file.write(reinterpret_cast<const char*>(&out_num_vertices), sizeof(node_id_t));
	out_file.write(reinterpret_cast<const char*>(&out_num_updates), sizeof(edge_id_t));
	if (!out_file) {
		std::cerr << "Error updating header in: " << out_filename << std::endl;
		return 1;
	}

	std::cout << "Input vertices (declared): " << in_num_vertices
						<< ", Input updates (declared): " << in_num_updates << std::endl;
	std::cout << "Output vertices (renumbered): " << out_num_vertices
						<< ", Output updates: " << out_num_updates << std::endl;

	return 0;
}
