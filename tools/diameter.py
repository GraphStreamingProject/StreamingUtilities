import networkx as nx
import numpy as np
import sys
import struct

#32 bits - number of vertices
#64 bits - number of edges

#Every edge: 8 bits of typing
#32 bits source
#32 bits sink


def diameter(G):
  arr = nx.floyd_warshall_numpy(G)
  # Convert inf to -inf for max
  return np.where(np.isinf(arr),-np.inf,arr).max()

  #max over all_pairs_bellman_ford_path_length(G)


def process(filename):
  G = nx.Graph()


  with open(filename, "rb") as file:
    nverts = file.read(4)
    nupds = file.read(8)
    while True:
      data = file.read(9)
      if not data:
        break;
      t, edge1, edge2 = struct.unpack("<bII", data)
      G.add_edge(edge1, edge2)
  return G


G = process(sys.argv[1]);
print(diameter(G))
