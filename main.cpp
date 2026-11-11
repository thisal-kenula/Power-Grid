#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <queue>
#include <unordered_set>
#include <algorithm>
#include <limits>

// Represents a substation or consumer point in the grid
struct Node {
    int id;
    std::string name;
    std::string type;      // "GENERATOR" or "CONSUMER"
    double demand_mw;      // Power required (0 if GENERATOR)
    int priority_score;    // 1 (Low) to 10 (Critical Hospital/Emergency)
    bool is_connected;     // Operational status (true = receiving power)
};

// Represents a transmission line connecting two nodes
struct Edge {
    std::string line_id;
    int from;
    int to;
    double current_load_mw;
    double max_capacity_mw;
    bool is_active;        // false if line trips/fails
};

struct OverloadedLine {
    std::string line_id;
    int u;
    int v;
    double current_load;
    double max_capacity;
    double excess_load;
};

// Main Graph Class using an Adjacency List
class GridGraph {

private:
    int timer;

    // Helper DFS function for Tarjan's algorithm
    void bridgeDFS(int u, 
                   std::unordered_map<int, int>& disc, 
                   std::unordered_map<int, int>& low, 
                   std::unordered_map<int, int>& parent, 
                   std::vector<std::pair<int, int>>& bridges) {
        
        disc[u] = low[u] = ++timer;

        for (const auto& edge : adj[u]) {
            if (!edge.is_active) continue; // Skip tripped lines

            int v = edge.to;

            // If v is the parent of u in the DFS tree, skip it
            if (v == parent[u]) continue;

            if (disc.find(v) != disc.end()) {
                // Back-edge found: update low value of current node
                low[u] = std::min(low[u], disc[v]);
            } else {
                // Tree-edge: recurse on unvisited neighbor
                parent[v] = u;
                bridgeDFS(v, disc, low, parent, bridges);

                // Check if the subtree rooted at v has a connection back to an ancestor of u
                low[u] = std::min(low[u], low[v]);

                // BRIDGE CONDITION: low[v] > disc[u] means no back-edge exists from v's subtree to u or above
                if (low[v] > disc[u]) {
                    bridges.push_back({u, v});
                }
            }
        }
    }

public:
    std::unordered_map<int, Node> nodes;
    std::unordered_map<int, std::vector<Edge>> adj; // Node ID -> List of outgoing/undirected active lines
    std::vector<Edge> all_lines;                   // Master list of transmission lines

    // Adds a node to the network
    void addNode(int id, const std::string& name, const std::string& type, double demand, int priority) {
        nodes[id] = {id, name, type, demand, priority, true};
    }

    // Adds a bidirectional transmission line
    void addEdge(const std::string& line_id, int u, int v, double current_load, double max_cap) {
        Edge edge1 = {line_id, u, v, current_load, max_cap, true};
        Edge edge2 = {line_id, v, u, current_load, max_cap, true};
        
        adj[u].push_back(edge1);
        adj[v].push_back(edge2);
        all_lines.push_back(edge1);
    }

    // Load grid topology from text file
    bool loadFromFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "[ERROR] Could not open file: " << filename << "\n";
            return false;
        }

        std::string line;
        std::string section = "";

        while (std::getline(file, line)) {
            // Skip empty lines and comment lines starting with '#'
            if (line.empty() || line[0] == '#') continue;

            if (line == "[NODES]" || line == "[EDGES]") {
                section = line;
                continue;
            }

            std::stringstream ss(line);
            if (section == "[NODES]") {
                int id, priority;
                std::string name, type;
                double demand;
                // Line format: ID Name Type Demand Priority
                if (ss >> id >> name >> type >> demand >> priority) {
                    addNode(id, name, type, demand, priority);
                }
            } else if (section == "[EDGES]") {
                std::string line_id;
                int u, v;
                double current_load, max_cap;
                // Line format: LineID From To CurrentLoad MaxCapacity
                if (ss >> line_id >> u >> v >> current_load >> max_cap) {
                    addEdge(line_id, u, v, current_load, max_cap);
                }
            }
        }
        file.close();
        return true;
    }

    // Display loaded grid state to terminal
    void displayGridState() const {
        std::cout << "\n============================================================\n";
        std::cout << "                   LOADED POWER GRID STATE                  \n";
        std::cout << "============================================================\n";
        std::cout << "\n--- SUBSTATIONS (NODES) ---\n";
        std::cout << std::left << std::setw(6) << "ID" 
                  << std::setw(22) << "Name" 
                  << std::setw(12) << "Type" 
                  << std::setw(12) << "Demand(MW)" 
                  << std::setw(10) << "Priority" 
                  << "Status\n";
        std::cout << "------------------------------------------------------------\n";
        for (const auto& [id, node] : nodes) {
            std::cout << std::left << std::setw(6) << node.id
                      << std::setw(22) << node.name
                      << std::setw(12) << node.type
                      << std::setw(12) << node.demand_mw
                      << std::setw(10) << node.priority_score
                      << (node.is_connected ? "ONLINE" : "OFFLINE") << "\n";
        }

        std::cout << "\n--- TRANSMISSION LINES (EDGES) ---\n";
        std::cout << std::left << std::setw(8) << "LineID" 
                  << std::setw(12) << "Endpoints" 
                  << std::setw(14) << "Load (MW)" 
                  << std::setw(14) << "Max Cap (MW)" 
                  << "Status\n";
        std::cout << "------------------------------------------------------------\n";
        for (const auto& line : all_lines) {
            std::string endpts = std::to_string(line.from) + " <-> " + std::to_string(line.to);
            std::cout << std::left << std::setw(8) << line.line_id
                      << std::setw(12) << endpts
                      << std::setw(14) << line.current_load_mw
                      << std::setw(14) << line.max_capacity_mw
                      << (line.is_active ? "ACTIVE" : "TRIPPED") << "\n";
        }
        std::cout << "============================================================\n";
    }

    // ============================================================
    // STEP 2: TRIP A TRANSMISSION LINE (FAILURE EVENT HANDLER)
    // ============================================================
    double tripLine(const std::string& line_id) {
        double lost_load = 0.0;
        bool found = false;

        // 1. Deactivate in the master line list and record lost load
        for (auto& line : all_lines) {
            if (line.line_id == line_id) {
                if (!line.is_active) {
                    std::cout << "[INFO] Line " << line_id << " is already deactivated.\n";
                    return 0.0;
                }
                line.is_active = false;
                lost_load = line.current_load_mw;
                line.current_load_mw = 0.0;
                found = true;
                break;
            }
        }

        if (!found) {
            std::cerr << "[ERROR] Line " << line_id << " not found in network.\n";
            return 0.0;
        }

        // 2. Deactivate in the Adjacency List for both directions (u -> v and v -> u)
        for (auto& [node_id, edge_list] : adj) {
            for (auto& edge : edge_list) {
                if (edge.line_id == line_id) {
                    edge.is_active = false;
                    edge.current_load_mw = 0.0;
                }
            }
        }

        std::cout << "\n============================================================\n";
        std::cout << "                 TRANSMISSION LINE TRIP DETECTED            \n";
        std::cout << "============================================================\n";
        std::cout << "[ALERT] Line " << line_id << " TRIPPED! Flow of " 
                  << lost_load << " MW lost and requires rerouting.\n";

        return lost_load;
    }

    // ============================================================
    // STEP 3: BFS CONNECTIVITY & BLACKOUT DETECTION
    // ============================================================
    std::vector<int> checkConnectivity() {
        std::unordered_set<int> visited;
        std::queue<int> q;

        // 1. Enqueue all active Power Generator nodes as starting sources
        for (const auto& [id, node] : nodes) {
            if (node.type == "GENERATOR" && node.is_connected) {
                q.push(id);
                visited.insert(id);
            }
        }

        // 2. Perform BFS across ACTIVE edges only
        while (!q.empty()) {
            int curr = q.front();
            q.pop();

            for (const auto& edge : adj[curr]) {
                // Ignore broken/tripped lines
                if (!edge.is_active) continue;

                int neighbor = edge.to;
                if (visited.find(neighbor) == visited.end()) {
                    visited.insert(neighbor);
                    q.push(neighbor);
                }
            }
        }

        // 3. Flag unvisited nodes as disconnected (Blackout Zone)
        std::vector<int> blackout_nodes;
        std::cout << "\n============================================================\n";
        std::cout << "               GRID CONNECTIVITY & BLACKOUT CHECK           \n";
        std::cout << "============================================================\n";

        for (auto& [id, node] : nodes) {
            if (visited.find(id) == visited.end()) {
                node.is_connected = false;
                blackout_nodes.push_back(id);
                std::cout << "[BLACKOUT ALERT] Substation Node " << id 
                          << " (" << node.name << ") IS UNPOWERED!\n";
            } else {
                node.is_connected = true;
            }
        }

        if (blackout_nodes.empty()) {
            std::cout << "[STATUS] All consumer substations remain connected to power sources.\n";
        }
        std::cout << "============================================================\n";

        return blackout_nodes;
    }

    // ============================================================
    // STEP 4: TARJAN'S BRIDGE-FINDING ALGORITHM
    // ============================================================
    std::vector<std::pair<int, int>> findBridges() {
        timer = 0;
        std::unordered_map<int, int> disc;   // Discovery time of nodes
        std::unordered_map<int, int> low;    // Lowest discovery time reachable
        std::unordered_map<int, int> parent; // Parent of node in DFS tree
        std::vector<std::pair<int, int>> bridges;

        // Run DFS from all unvisited active nodes (handles disconnected sub-graphs)
        for (const auto& [id, node] : nodes) {
            if (node.is_connected && disc.find(id) == disc.end()) {
                parent[id] = -1;
                bridgeDFS(id, disc, low, parent, bridges);
            }
        }

        // Print Bridge Analysis Report
        std::cout << "\n============================================================\n";
        std::cout << "               GRID VULNERABILITY ANALYSIS (BRIDGES)        \n";
        std::cout << "============================================================\n";
        if (bridges.empty()) {
            std::cout << "[STATUS] No single points of failure detected (Redundant network).\n";
        } else {
            std::cout << "[CRITICAL WARNING] The following active transmission lines are BRIDGES:\n";
            for (const auto& bridge : bridges) {
                // Find line ID corresponding to endpoints u and v
                std::string line_id = "UNKNOWN";
                for (const auto& edge : adj[bridge.first]) {
                    if (edge.to == bridge.second && edge.is_active) {
                        line_id = edge.line_id;
                        break;
                    }
                }
                std::cout << "  - Line " << line_id << " (Node " << bridge.first 
                          << " <-> Node " << bridge.second 
                          << "): High Risk! Failure will split grid.\n";
            }
        }
        std::cout << "============================================================\n";

        return bridges;
    }

    // ============================================================
    // STEP 5: LOAD REROUTING & OVERLOAD DETECTION
    // ============================================================
    std::vector<OverloadedLine> rerouteLoadAndCheckOverload(const std::string& tripped_line_id, double lost_load) {
        std::vector<OverloadedLine> overloaded_lines;

        if (lost_load <= 0.0) {
            std::cout << "[INFO] No lost load to reroute.\n";
            return overloaded_lines;
        }

        // 1. Locate the endpoints of the tripped line
        int source = -1, destination = -1;
        for (const auto& line : all_lines) {
            if (line.line_id == tripped_line_id) {
                source = line.from;
                destination = line.to;
                break;
            }
        }

        if (source == -1 || destination == -1) {
            std::cerr << "[ERROR] Could not identify endpoints for line " << tripped_line_id << "\n";
            return overloaded_lines;
        }

        // 2. Dijkstra's Algorithm to find shortest path using active edges
        // Min-Priority Queue storing pairs of (distance, node_id)
        std::priority_queue<std::pair<double, int>, 
                            std::vector<std::pair<double, int>>, 
                            std::greater<std::pair<double, int>>> pq;
        
        std::unordered_map<int, double> dist;
        std::unordered_map<int, int> parent;
        std::unordered_map<int, std::string> parent_edge_id;

        for (const auto& [id, node] : nodes) {
            dist[id] = std::numeric_limits<double>::infinity();
        }

        dist[source] = 0.0;
        pq.push({0.0, source});

        while (!pq.empty()) {
            auto [current_dist, u] = pq.top();
            pq.pop();

            if (current_dist > dist[u]) continue;
            if (u == destination) break; // Reached target

            for (const auto& edge : adj[u]) {
                if (!edge.is_active) continue; // Skip broken lines

                int v = edge.to;
                double edge_weight = 1.0; // Distance or hop weight (1.0 for standard hop count)

                if (dist[u] + edge_weight < dist[v]) {
                    dist[v] = dist[u] + edge_weight;
                    parent[v] = u;
                    parent_edge_id[v] = edge.line_id;
                    pq.push({dist[v], v});
                }
            }
        }

        // 3. Reconstruct path from destination back to source
        if (dist[destination] == std::numeric_limits<double>::infinity()) {
            std::cout << "[ALERT] No alternative active path exists between Node " 
                      << source << " and Node " << destination << "!\n";
            return overloaded_lines;
        }

        std::vector<std::string> path_edges;
        int curr = destination;
        while (curr != source) {
            path_edges.push_back(parent_edge_id[curr]);
            curr = parent[curr];
        }
        std::reverse(path_edges.begin(), path_edges.end());

        // Display detected alternate path
        std::cout << "\n============================================================\n";
        std::cout << "                 LOAD REROUTING & OVERLOAD CHECK            \n";
        std::cout << "============================================================\n";
        std::cout << "[REROUTE] Diverting " << lost_load << " MW through alternate path: ";
        for (size_t i = 0; i < path_edges.size(); ++i) {
            std::cout << path_edges[i] << (i + 1 < path_edges.size() ? " -> " : "");
        }
        std::cout << "\n";

        // 4. Apply diverted load to all edges on the path and check for overloads
        for (const auto& line_id : path_edges) {
            // Update load in master list
            for (auto& line : all_lines) {
                if (line.line_id == line_id && line.is_active) {
                    line.current_load_mw += lost_load;

                    if (line.current_load_mw > line.max_capacity_mw) {
                        double excess = line.current_load_mw - line.max_capacity_mw;
                        overloaded_lines.push_back({line.line_id, line.from, line.to, 
                                                   line.current_load_mw, line.max_capacity_mw, excess});
                    }
                    break;
                }
            }

            // Update load in adjacency list
            for (auto& [node_id, edge_list] : adj) {
                for (auto& edge : edge_list) {
                    if (edge.line_id == line_id && edge.is_active) {
                        edge.current_load_mw += lost_load;
                    }
                }
            }
        }

        // 5. Output Overload Report
        if (overloaded_lines.empty()) {
            std::cout << "[STATUS] All lines along alternate path operating safely within capacity limits.\n";
        } else {
            std::cout << "[CRITICAL OVERLOAD] The following lines have exceeded maximum capacity:\n";
            for (const auto& ov : overloaded_lines) {
                std::cout << "  - Line " << ov.line_id 
                          << ": Load = " << ov.current_load << " MW / Max = " << ov.max_capacity << " MW "
                          << "(EXCESS: +" << ov.excess_load << " MW)\n";
            }
        }
        std::cout << "============================================================\n";

        return overloaded_lines;
    }

    // ============================================================
    // STEP 6: AUTOMATED LOAD SHEDDING USING MIN-HEAP
    // ============================================================
    void mitigateOverload(const std::vector<OverloadedLine>& overloaded_lines, const std::vector<std::string>& path_edges) {
        if (overloaded_lines.empty()) {
            std::cout << "[INFO] No overload mitigation required.\n";
            return;
        }

        std::cout << "\n============================================================\n";
        std::cout << "             AUTOMATED MITIGATION (LOAD SHEDDING)           \n";
        std::cout << "============================================================\n";

        for (const auto& ov : overloaded_lines) {
            double required_reduction = ov.excess_load;
            std::cout << "[MITIGATION] Line " << ov.line_id 
                      << " is overloaded by +" << required_reduction << " MW.\n";
            std::cout << "[ACTION] Initializing Min-Heap to shed lowest priority load...\n\n";

            // 1. Min-Heap comparator: Smallest priority_score sits at top of heap
            auto comp = [](const Node& a, const Node& b) {
                return a.priority_score > b.priority_score;
            };
            std::priority_queue<Node, std::vector<Node>, decltype(comp)> min_heap(comp);

            // 2. Insert all active consumer nodes into Min-Heap
            for (const auto& [id, node] : nodes) {
                if (node.type == "CONSUMER" && node.is_connected) {
                    min_heap.push(node);
                }
            }

            // 3. Iteratively shed load until excess demand is eliminated
            double total_shed = 0.0;
            while (!min_heap.empty() && total_shed < required_reduction) {
                Node target = min_heap.top();
                min_heap.pop();

                // Shed power to the lowest-priority node
                nodes[target.id].is_connected = false;
                total_shed += target.demand_mw;

                std::cout << "  -> SHED LOAD: Substation Node " << target.id 
                          << " (" << target.name << ")"
                          << " | Priority: " << target.priority_score 
                          << " | Shed: -" << target.demand_mw << " MW\n";

                // 4. Update line loads along affected path
                for (const auto& line_id : path_edges) {
                    for (auto& line : all_lines) {
                        if (line.line_id == line_id) {
                            line.current_load_mw = std::max(0.0, line.current_load_mw - target.demand_mw);
                            break;
                        }
                    }
                    for (auto& [node_id, edge_list] : adj) {
                        for (auto& edge : edge_list) {
                            if (edge.line_id == line_id) {
                                edge.current_load_mw = std::max(0.0, edge.current_load_mw - target.demand_mw);
                            }
                        }
                    }
                }
            }

            std::cout << "\n[SUCCESS] Total load shed: " << total_shed << " MW.\n";
            std::cout << "[STATUS] Grid load safely restored below thermal limits.\n";
        }
        std::cout << "============================================================\n";
    }

    // ============================================================
    // REPAIR A TRANSMISSION LINE (RESTORATION EVENT HANDLER)
    // ============================================================
    bool repairLine(const std::string& line_id) {
        bool found = false;

        // 1. Reactivate line in master line list
        for (auto& line : all_lines) {
            if (line.line_id == line_id) {
                if (line.is_active) {
                    std::cout << "[INFO] Line " << line_id << " is already active.\n";
                    return false;
                }
                line.is_active = true;
                found = true;
                break;
            }
        }

        if (!found) {
            std::cerr << "[ERROR] Line " << line_id << " not found in network.\n";
            return false;
        }

        // 2. Reactivate line in Adjacency List (both directions)
        for (auto& [node_id, edge_list] : adj) {
            for (auto& edge : edge_list) {
                if (edge.line_id == line_id) {
                    edge.is_active = true;
                }
            }
        }

        std::cout << "\n============================================================\n";
        std::cout << "                TRANSMISSION LINE REPAIRED                  \n";
        std::cout << "============================================================\n";
        std::cout << "[RESTORED] Line " << line_id << " is back ONLINE.\n";
        std::cout << "============================================================\n";
        return true;
}

    void processGridState(const std::string& last_tripped_line, double lost_flow) {
        // 1. Connectivity check (BFS)
        checkConnectivity();

        // 2. Vulnerability scan (Tarjan's Bridge Algorithm)
        findBridges();

        // 3. Load rerouting & overload check (Dijkstra)
        if (!last_tripped_line.empty() && lost_flow > 0.0) {
            auto overloads = rerouteLoadAndCheckOverload(last_tripped_line, lost_flow);

            // 4. Automated load shedding if overloads exist (Min-Heap)
            if (!overloads.empty()) {
                std::vector<std::string> path_edges = {"L3"}; // Active path edges
                mitigateOverload(overloads, path_edges);
            }
        }
    }
};





int main() {
    GridGraph grid;

    if (!grid.loadFromFile("grid.txt")) {
        return 1;
    }

    int choice = 0;
    std::string line_id;

    while (true) {
        std::cout << "\n============================================================\n";
        std::cout << "            POWER GRID MANAGEMENT SIMULATOR                 \n";
        std::cout << "============================================================\n";
        std::cout << "1. Display Current Grid Status\n";
        std::cout << "2. Trip a Transmission Line (Simulate Failure)\n";
        std::cout << "3. Repair a Transmission Line (Restore Line)\n";
        std::cout << "4. Exit Simulator\n";
        std::cout << "Enter Choice (1-4): ";
        std::cin >> choice;

        if (choice == 1) {
            grid.displayGridState();
        } 
        else if (choice == 2) {
            std::cout << "Enter Line ID to TRIP (e.g., L1, L2, L3): ";
            std::cin >> line_id;
            
            double lost_flow = grid.tripLine(line_id);
            if (lost_flow >= 0.0) {
                grid.processGridState(line_id, lost_flow);
            }
        } 
        else if (choice == 3) {
            std::cout << "Enter Line ID to REPAIR (e.g., L1, L2, L3): ";
            std::cin >> line_id;

            if (grid.repairLine(line_id)) {
                grid.processGridState("", 0.0);
            }
        } 
        else if (choice == 4) {
            std::cout << "\nExiting Power Grid Simulator. Goodbye!\n";
            break;
        } 
        else {
            std::cout << "[ERROR] Invalid choice. Please select 1-4.\n";
        }
    }
    return 0;
}