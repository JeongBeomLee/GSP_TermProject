#include <iostream>
#include <array>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <concurrent_priority_queue.h>
#include <mutex>
#include <fstream>
#include <map>
#include <chrono>
#include "protocol.h"
#include "include/lua.hpp"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "lua54.lib")
using namespace std;

array<array<unordered_set<int>, SECTOR_ROWS>, SECTOR_COLS> g_sectors;
array<array<mutex, SECTOR_ROWS>, SECTOR_COLS> sector_locks;
array<POINT, NUM_OBSTACLES> obstacles;
array<array<vector<POINT>, SECTOR_ROWS>, SECTOR_COLS> obstacle_grid;

bool is_obstacle(int x, int y) {
    int sector_x = x / SECTOR_WIDTH;
    int sector_y = y / SECTOR_HEIGHT;
    for (const auto& obstacle : obstacle_grid[sector_x][sector_y]) {
        if (obstacle.x == x && obstacle.y == y) {
            return true;
        }
    }
    return false;
}

struct Node {
    int x, y;
    int f, g, h;
    bool operator<(const Node& other) const {
        return f > other.f; // for priority queue to get the node with the lowest f
    }
};

vector<POINT> AStarPathfinding(int startX, int startY, int goalX, int goalY) {
    priority_queue<Node> openList;
    map<pair<int, int>, pair<int, int>> cameFrom;
    map<pair<int, int>, int> costSoFar;

    auto heuristic = [](int x1, int y1, int x2, int y2) {
        return abs(x1 - x2) + abs(y1 - y2);
        };

    openList.push({ startX, startY, 0, 0, heuristic(startX, startY, goalX, goalY) });
    cameFrom[{startX, startY}] = { startX, startY };
    costSoFar[{startX, startY}] = 0;

    while (!openList.empty()) {
        Node current = openList.top();
        openList.pop();

        if (current.x == goalX && current.y == goalY) {
            vector<POINT> path;
            while (!(current.x == startX && current.y == startY)) {
                path.push_back({ current.x, current.y });
                auto prev = cameFrom[{current.x, current.y}];
                current.x = prev.first;
                current.y = prev.second;
            }
            reverse(path.begin(), path.end());
            return path;
        }

		static int dx[] = { 0, 0, -1, 1 };
		static int dy[] = { -1, 1, 0, 0 };
        for (int i = 0; i < 4; ++i) {
            int nx = current.x + dx[i], ny = current.y + dy[i];
            if (nx < 0 || ny < 0 || nx >= W_WIDTH || ny >= W_HEIGHT) continue;
            if (is_obstacle(nx, ny)) continue;

            int newCost = costSoFar[{current.x, current.y}] + 1;
            if (!costSoFar.count({ nx, ny }) || newCost < costSoFar[{nx, ny}]) {
                costSoFar[{nx, ny}] = newCost;
                int priority = newCost + heuristic(nx, ny, goalX, goalY);
                openList.push({ nx, ny, priority, newCost, heuristic(nx, ny, goalX, goalY) });
                cameFrom[{nx, ny}] = { current.x, current.y };
            }
        }
    }

    return {};
}

constexpr int NPC_RESPAWN_TIME = 30;

enum EVENT_TYPE { EV_RANDOM_MOVE, EV_RESPAWN, EV_TRACE, EV_HEAL };
struct TIMER_EVENT {
    int obj_id;
    chrono::system_clock::time_point wakeup_time;
    EVENT_TYPE event_id;
    int target_id;
    constexpr bool operator < (const TIMER_EVENT& L) const {
        return (wakeup_time > L.wakeup_time);
    }
};
concurrency::concurrent_priority_queue<TIMER_EVENT> timer_queue;

enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND, OP_NPC_MOVE, OP_PLAYER_MOVE, OP_RESPAWN, OP_TRACE, OP_HEAL };
class OVER_EXP {
public:
    WSAOVERLAPPED _over;
    WSABUF _wsabuf;
    char _send_buf[BUF_SIZE];
    COMP_TYPE _comp_type;
    int _ai_target_obj;
    OVER_EXP() {
        _wsabuf.len = BUF_SIZE;
        _wsabuf.buf = _send_buf;
        _comp_type = OP_RECV;
        ZeroMemory(&_over, sizeof(_over));
    }
    OVER_EXP(char* packet) {
        _wsabuf.len = packet[0];
        _wsabuf.buf = _send_buf;
        ZeroMemory(&_over, sizeof(_over));
        _comp_type = OP_SEND;
        memcpy(_send_buf, packet, packet[0]);
    }
};

enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };
class SESSION {
    OVER_EXP _recv_over;
public:
    mutex _s_lock;
    S_STATE _state;
    atomic_bool _is_active;  // 주위에 플레이어가 있는가?
    int _id;
    SOCKET _socket;
    short x, y;
	short start_x, start_y;
    char _name[NAME_SIZE];
    unordered_set<int> _view_list;
    mutex _vl;
    int last_move_time;
    lua_State* _L;
    mutex _ll;
    char _ai_dir;
    int _sector_x, _sector_y;
    Visual _visual;
    int exp, max_exp;
    int hp, max_hp;
    int level;
    int attack_damage;
    int npc_respawn_time;
    vector<char> _recv_buffer; // 수신된 데이터를 누적할 동적 버퍼

    SESSION() {
        _id = -1;
        _socket = 0;
        x = y = 0;
        _name[0] = 0;
        _state = ST_FREE;
        _ai_dir = -1;
    }

    ~SESSION() {}

    void do_recv() {
        DWORD recv_flag = 0;
        memset(&_recv_over._over, 0, sizeof(_recv_over._over));
        _recv_over._wsabuf.len = BUF_SIZE;
        _recv_over._wsabuf.buf = _recv_over._send_buf;
        WSARecv(_socket, &_recv_over._wsabuf, 1, 0, &recv_flag, &_recv_over._over, 0);
    }

    void do_send(void* packet) {
        OVER_EXP* sdata = new OVER_EXP{ reinterpret_cast<char*>(packet) };
        WSASend(_socket, &sdata->_wsabuf, 1, 0, 0, &sdata->_over, 0);
    }
    void send_login_info_packet() {
        SC_LOGIN_INFO_PACKET p;
        p.id = _id;
        p.size = sizeof(SC_LOGIN_INFO_PACKET);
        p.type = SC_LOGIN_INFO;
		p.exp = exp;
        p.hp = hp;
        p.level = level;
		p.max_hp = max_hp;
        p.visual = static_cast<int>(Visual::LEA);
        p.x = x;
        p.y = y;
        do_send(&p);
#ifdef DEBUG
        cout << "Login info packet sent to client[" << _id << "]\n";
#endif
    }
    void send_move_packet(int c_id);
    void send_add_player_packet(int c_id);
    void send_chat_packet(int c_id, const char* mess);
	void send_stat_change_packet() {
        SC_STAT_CHANGE_PACKET p;
		p.size = sizeof(SC_STAT_CHANGE_PACKET);
		p.type = SC_STAT_CHANGE;
        p.exp = exp;
		p.hp = hp;
		p.level = level;
		p.max_hp = max_hp;
		do_send(&p);
	}

    void send_remove_player_packet(int c_id) {
        _vl.lock();
        if (_view_list.count(c_id))
            _view_list.erase(c_id);
        else {
            _vl.unlock();
            return;
        }
        _vl.unlock();
        SC_REMOVE_OBJECT_PACKET p;
        p.id = c_id;
        p.size = sizeof(p);
        p.type = SC_REMOVE_OBJECT;
        do_send(&p);
    }

	void send_player_attack_packet(int c_id) {
		SC_PLAYER_ATTACK_PACKET p;
		p.id = c_id;
		p.size = sizeof(SC_PLAYER_ATTACK_PACKET);
		p.type = SC_PLAYER_ATTACK;
		do_send(&p);
	}
};

HANDLE h_iocp;
array<SESSION, MAX_USER + MAX_NPC> clients;
SOCKET g_s_socket, g_c_socket;
OVER_EXP g_a_over;

bool is_pc(int object_id) {
    return object_id < MAX_USER;
}

bool is_npc(int object_id) {
    return !is_pc(object_id);
}

bool can_see(int from, int to) {
    if (abs(clients[from]._sector_x - clients[to]._sector_x) > 1) return false;
    if (abs(clients[from]._sector_y - clients[to]._sector_y) > 1) return false;

    int dist = (clients[from].x - clients[to].x) * (clients[from].x - clients[to].x) +
        (clients[from].y - clients[to].y) * (clients[from].y - clients[to].y);
    return dist <= VIEW_RANGE * VIEW_RANGE;
}

// 두 좌표가 인접(상하좌우)한지 판단
bool is_adjacent(int x1, int y1, int x2, int y2) {
    return (abs(x1 - x2) + abs(y1 - y2)) == 1;
}

bool is_attacked_from_player(int c_id, int npc_id) {
    return is_adjacent(clients[c_id].x, clients[c_id].y, clients[npc_id].x, clients[npc_id].y);
}

void SESSION::send_move_packet(int c_id) {
    SC_MOVE_OBJECT_PACKET p;
    p.id = c_id;
    p.size = sizeof(SC_MOVE_OBJECT_PACKET);
    p.type = SC_MOVE_OBJECT;
    p.x = clients[c_id].x;
    p.y = clients[c_id].y;
    p.move_time = clients[c_id].last_move_time;
    do_send(&p);
}

void SESSION::send_add_player_packet(int c_id) {
    SC_ADD_OBJECT_PACKET add_packet;
    add_packet.id = c_id;
    strcpy_s(add_packet.name, clients[c_id]._name);
    add_packet.size = sizeof(add_packet);
    add_packet.type = SC_ADD_OBJECT;
    add_packet.x = clients[c_id].x;
    add_packet.y = clients[c_id].y;
    add_packet.visual = static_cast<int>(clients[c_id]._visual);
    _vl.lock();
    _view_list.insert(c_id);
    _vl.unlock();
    do_send(&add_packet);
}

void SESSION::send_chat_packet(int p_id, const char* mess) {
    SC_CHAT_PACKET packet;
    packet.id = p_id;
    packet.size = sizeof(packet);
    packet.type = SC_CHAT;
    strcpy_s(packet.mess, mess);
    do_send(&packet);
}

int get_new_client_id() {
    for (int i = 0; i < MAX_USER; ++i) {
        lock_guard<mutex> ll{ clients[i]._s_lock };
        if (clients[i]._state == ST_FREE)
            return i;
    }
    return -1;
}

// 몬스터 1 (Peace, 고정)
// 몬스터 2 (Peace, 랜덤움직임)
// 몬스터 3 (Aggro, 고정)
// 몬스터 4 (Aggro, 랜덤움직임)
void WakeUpNPC(int npc_id, int waker) {
    if (clients[npc_id]._is_active) return;
    bool old_state = false;
    if (false == atomic_compare_exchange_strong(&clients[npc_id]._is_active, &old_state, true)) return;

    switch (clients[npc_id]._visual) {
        case BALOONEER:
            // Peace, 고정

            break;
        case PINCERON:
            // Peace, 랜덤움직임
            timer_queue.push(TIMER_EVENT{ npc_id, chrono::system_clock::now() + 1s, EV_RANDOM_MOVE, 0 });
            break;
        case FROBBIT:
            // Aggro, 고정
            timer_queue.push(TIMER_EVENT{ npc_id, chrono::system_clock::now() + 1s, EV_TRACE, waker });
            break;
        case HEDGEHAG:
            // Aggro, 고정
            timer_queue.push(TIMER_EVENT{ npc_id, chrono::system_clock::now() + 1s, EV_TRACE, waker });
            break;
    }
}

void process_packet(int c_id, char* packet) {
#ifdef DEBUG
    //cout << "Packet received from client[" << c_id << "]\n";
#endif
    switch (packet[2]) {
    case CS_LOGIN: {
        CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);
        strcpy_s(clients[c_id]._name, p->name);
        {
            lock_guard<mutex> ll{ clients[c_id]._s_lock };
			clients[c_id].x = rand() % W_WIDTH;
			clients[c_id].y = rand() % W_HEIGHT;
            clients[c_id].start_x = clients[c_id].x;
			clients[c_id].start_y = clients[c_id].y;
            clients[c_id]._state = ST_INGAME;
            clients[c_id]._sector_x = clients[c_id].x / SECTOR_WIDTH;
            clients[c_id]._sector_y = clients[c_id].y / SECTOR_HEIGHT;
            clients[c_id].exp = 0;
            clients[c_id].max_exp = 100;
            clients[c_id].hp = 100;
            clients[c_id].max_hp = 100;
            clients[c_id].level = 1;
            clients[c_id].attack_damage = 10;

            sector_locks[clients[c_id]._sector_x][clients[c_id]._sector_y].lock();
            g_sectors[clients[c_id]._sector_x][clients[c_id]._sector_y].insert(c_id);
            sector_locks[clients[c_id]._sector_x][clients[c_id]._sector_y].unlock();

            // 5초에 한 번씩 최대 체력 10%의 HP를 회복
			TIMER_EVENT ev{ c_id, chrono::system_clock::now() + 5s, EV_HEAL, 0 };
			timer_queue.push(ev);
        }
#ifdef DEBUG
        cout << "Client[" << c_id << "] logged in.\n";
#endif
        clients[c_id].send_login_info_packet();
        for (int y = max(clients[c_id]._sector_y - 1, 0); y <= min(clients[c_id]._sector_y + 1, SECTOR_ROWS - 1); ++y) {
            for (int x = max(clients[c_id]._sector_x - 1, 0); x <= min(clients[c_id]._sector_x + 1, SECTOR_COLS - 1); ++x) {
                lock_guard<mutex> lock(sector_locks[x][y]);
                for (int p_id : g_sectors[x][y]) {
                    {
                        lock_guard<mutex> ll(clients[p_id]._s_lock);
                        if (ST_INGAME != clients[p_id]._state) continue;
                    }
                    if (p_id == c_id) continue;
                    if (false == can_see(c_id, p_id)) continue;
                    if (is_pc(p_id)) clients[p_id].send_add_player_packet(c_id);
                    else WakeUpNPC(p_id, c_id);
                    clients[c_id].send_add_player_packet(p_id);
                }
            }
        }
        break;
    }

    case CS_MOVE: {
        CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
        clients[c_id].last_move_time = p->move_time;
        short x = clients[c_id].x;
        short y = clients[c_id].y;
        short new_x = x;
        short new_y = y;
        switch (p->direction) {
        case 0:
            if (y > 0) new_y--; break;
        case 1:
            if (y < W_HEIGHT - 1) new_y++; break;
        case 2:
            if (x > 0) new_x--; break;
        case 3:
            if (x < W_WIDTH - 1) new_x++; break;
        }

        if (!is_obstacle(new_x, new_y)) {
            bool collision = false;
            // 전체 객체(플레이어 및 NPC)를 대상으로 겹침 여부 검사
            for (int i = 0; i < MAX_USER + MAX_NPC; ++i) {
                if (i == c_id) continue;
                if (clients[i]._state != ST_INGAME) continue;
                if (clients[i].x == new_x && clients[i].y == new_y) {
                    collision = true;
                    break;
                }
            }
            if (!collision) {
                clients[c_id].x = new_x;
                clients[c_id].y = new_y;
            }
        }
#ifdef DEBUG
        cout << "Client[" << c_id << "] moved to (" << x << ", " << y << ")\n";
#endif

        int old_sector_x = clients[c_id]._sector_x;
        int old_sector_y = clients[c_id]._sector_y;
        int new_sector_x = x / SECTOR_WIDTH;
        int new_sector_y = y / SECTOR_HEIGHT;

        if (old_sector_x != new_sector_x || old_sector_y != new_sector_y) {
            sector_locks[old_sector_x][old_sector_y].lock();
            g_sectors[old_sector_x][old_sector_y].erase(c_id);
            sector_locks[old_sector_x][old_sector_y].unlock();

            sector_locks[new_sector_x][new_sector_y].lock();
            g_sectors[new_sector_x][new_sector_y].insert(c_id);
            sector_locks[new_sector_x][new_sector_y].unlock();

            clients[c_id]._sector_x = new_sector_x;
            clients[c_id]._sector_y = new_sector_y;
        }

        unordered_set<int> near_list;
        clients[c_id]._vl.lock();
        unordered_set<int> old_vlist = clients[c_id]._view_list;
        clients[c_id]._vl.unlock();
        for (int y = max(new_sector_y - 1, 0); y <= min(new_sector_y + 1, SECTOR_ROWS - 1); ++y) {
            for (int x = max(new_sector_x - 1, 0); x <= min(new_sector_x + 1, SECTOR_COLS - 1); ++x) {
                lock_guard<mutex> lock(sector_locks[x][y]);
                for (int cl_id : g_sectors[x][y]) {
                    if (clients[cl_id]._state != ST_INGAME) continue;
                    if (cl_id == c_id) continue;
                    if (can_see(c_id, cl_id))
                        near_list.insert(cl_id);
                }
            }
        }

        clients[c_id].send_move_packet(c_id);

        for (auto& pl : near_list) {
            auto& cpl = clients[pl];
            if (is_pc(pl)) {
                cpl._vl.lock();
                if (clients[pl]._view_list.count(c_id)) {
                    cpl._vl.unlock();
                    clients[pl].send_move_packet(c_id);
                }
                else {
                    cpl._vl.unlock();
                    clients[pl].send_add_player_packet(c_id);
                }
            }
            else WakeUpNPC(pl, c_id);

            if (old_vlist.count(pl) == 0)
                clients[c_id].send_add_player_packet(pl);
        }

        for (auto& pl : old_vlist)
            if (0 == near_list.count(pl)) {
                clients[c_id].send_remove_player_packet(pl);
                if (is_pc(pl))
                    clients[pl].send_remove_player_packet(c_id);
            }
    }
                break;

    case CS_ATTACK: {
#ifdef DEBUG
		cout << "Client[" << c_id << "] attack\n";
#endif
        for (int y = max(clients[c_id]._sector_y - 1, 0); y <= min(clients[c_id]._sector_y + 1, SECTOR_ROWS - 1); ++y) {
            for (int x = max(clients[c_id]._sector_x - 1, 0); x <= min(clients[c_id]._sector_x + 1, SECTOR_COLS - 1); ++x) {
                lock_guard<mutex> lock(sector_locks[x][y]);
                for (int npc_id : g_sectors[x][y]) {
                    if (clients[npc_id]._state != ST_INGAME) continue;
                    if (is_pc(npc_id)) continue;
                    if (false == can_see(c_id, npc_id)) continue;
                    if (is_attacked_from_player(c_id, npc_id)) {
#ifdef DEBUG
                        cout << "Client[" << c_id << "] attacked NPC[" << npc_id << "]\n";
#endif
                        clients[npc_id].hp -= clients[c_id].attack_damage;
                        if (clients[npc_id]._visual == Visual::PINCERON) {
							clients[c_id].hp -= clients[npc_id].attack_damage;
							clients[c_id].send_stat_change_packet();
                        }
                        if (clients[npc_id]._visual == Visual::BALOONEER) {
							TIMER_EVENT ev{ npc_id, chrono::system_clock::now(), EV_TRACE, 0 };
							timer_queue.push(ev);
                        }

                        if (clients[npc_id].hp <= 0) {
#ifdef DEBUG
                            cout << "NPC[" << npc_id << "] is dead from Client[" << c_id << "]\n";
#endif
                            clients[npc_id].hp = 0;
                            clients[c_id].send_remove_player_packet(npc_id);
                            clients[npc_id]._state = ST_FREE;
                            TIMER_EVENT ev{ npc_id, chrono::system_clock::now() + chrono::seconds(NPC_RESPAWN_TIME), EV_RESPAWN, 0 };
                            timer_queue.push(ev);

                            clients[c_id].exp += 2 * pow(clients[npc_id].level, 2);

							if (clients[c_id].exp >= clients[c_id].max_exp) {
								clients[c_id].level++;
                                clients[c_id].max_exp = clients[c_id].max_exp * 2;
                                clients[c_id].exp = 0;
								clients[c_id].max_hp = 100 * clients[c_id].level;
								clients[c_id].hp = clients[c_id].max_hp;
								clients[c_id].attack_damage = 10 * pow(clients[c_id].level, 2);
                            }
                            clients[c_id].send_stat_change_packet();
                        }
                    }
                }

				for (int other_id : g_sectors[x][y]) {
					if (clients[other_id]._state != ST_INGAME) continue;
					if (is_npc(other_id)) continue;
					if (false == can_see(c_id, other_id)) continue;
					clients[other_id].send_player_attack_packet(c_id);
				}
            }
        }
    }
        break;

    case CS_CHAT: {
		CS_CHAT_PACKET* p = reinterpret_cast<CS_CHAT_PACKET*>(packet);
#ifdef DEBUG
		cout << "Client[" << c_id << "] chat : " << p->mess << "\n";
#endif
        for (int y = max(clients[c_id]._sector_y - 1, 0); y <= min(clients[c_id]._sector_y + 1, SECTOR_ROWS - 1); ++y) {
			for (int x = max(clients[c_id]._sector_x - 1, 0); x <= min(clients[c_id]._sector_x + 1, SECTOR_COLS - 1); ++x) {
				lock_guard<mutex> lock(sector_locks[x][y]);
                for (int pl_id : g_sectors[x][y]) {
					if (clients[pl_id]._state != ST_INGAME) continue;
					if (false == can_see(c_id, pl_id)) continue;
					if (false == is_pc(pl_id)) continue;
					if (0 == strcmp(p->mess, "")) continue;

					clients[pl_id].send_chat_packet(c_id, p->mess);
				}
			}
        }
    }
        break;

    default:
		cout << "unknown packet type\n";
        break;
    }
}

void disconnect(int c_id) {
    clients[c_id]._vl.lock();
    unordered_set<int> vl = clients[c_id]._view_list;
    clients[c_id]._vl.unlock();
    for (auto& p_id : vl) {
        if (is_npc(p_id)) continue;
        auto& pl = clients[p_id];
        {
            lock_guard<mutex> ll(pl._s_lock);
            if (ST_INGAME != pl._state) continue;
        }
        if (pl._id == c_id) continue;
        pl.send_remove_player_packet(c_id);
    }

    sector_locks[clients[c_id]._sector_x][clients[c_id]._sector_y].lock();
    g_sectors[clients[c_id]._sector_x][clients[c_id]._sector_y].erase(c_id);
    sector_locks[clients[c_id]._sector_x][clients[c_id]._sector_y].unlock();

    closesocket(clients[c_id]._socket);

    lock_guard<mutex> ll(clients[c_id]._s_lock);
    clients[c_id]._state = ST_FREE;
}

void do_npc_random_move(int npc_id) {
    SESSION& npc = clients[npc_id];
    unordered_set<int> old_vl;
    for (auto& obj : clients) {
        if (ST_INGAME != obj._state) continue;
        if (true == is_npc(obj._id)) continue;
        if (true == can_see(npc._id, obj._id))
            old_vl.insert(obj._id);
    }

    int x = npc.x;
    int y = npc.y;
    int new_x = x;
    int new_y = y;

    switch (rand() % 4) {
    case 0: if (x < (W_WIDTH - 1)) new_x++; break;
    case 1: if (x > 0) new_x--; break;
    case 2: if (y < (W_HEIGHT - 1)) new_y++; break;
    case 3:if (y > 0) new_y--; break;
    }

    // 이동하려는 좌표에 장애물이 없고, 다른 플레이어 및 NPC와 겹치지 않을 경우에만 이동
    if (!is_obstacle(new_x, new_y)) {
        bool collision = false;
        for (int i = 0; i < MAX_USER + MAX_NPC; ++i) {
            if (i == npc_id) continue;
            if (clients[i]._state != ST_INGAME) continue;
            if (clients[i].x == new_x && clients[i].y == new_y) {
                collision = true;
                break;
            }
        }
        if (!collision) {
            npc.x = new_x;
            npc.y = new_y;
        }
    }

    int old_sector_x = npc._sector_x;
    int old_sector_y = npc._sector_y;
    int new_sector_x = x / SECTOR_WIDTH;
    int new_sector_y = y / SECTOR_HEIGHT;

    if (old_sector_x != new_sector_x || old_sector_y != new_sector_y) {
        sector_locks[old_sector_x][old_sector_y].lock();
        g_sectors[old_sector_x][old_sector_y].erase(npc_id);
        sector_locks[old_sector_x][old_sector_y].unlock();

        sector_locks[new_sector_x][new_sector_y].lock();
        g_sectors[new_sector_x][new_sector_y].insert(npc_id);
        sector_locks[new_sector_x][new_sector_y].unlock();

        npc._sector_x = new_sector_x;
        npc._sector_y = new_sector_y;
    }

    unordered_set<int> new_vl;
    for (int y = max(new_sector_y - 1, 0); y <= min(new_sector_y + 1, SECTOR_ROWS - 1); ++y) {
        for (int x = max(new_sector_x - 1, 0); x <= min(new_sector_x + 1, SECTOR_COLS - 1); ++x) {
            lock_guard<mutex> lock(sector_locks[x][y]);
            for (int cl_id : g_sectors[x][y]) {
                if (clients[cl_id]._state != ST_INGAME) continue;
                if (true == is_npc(cl_id)) continue;
                if (can_see(npc_id, cl_id))
                    new_vl.insert(cl_id);
            }
        }
    }

    for (auto pl : new_vl) {
        if (0 == old_vl.count(pl)) {
            // 플레이어의 시야에 등장
            clients[pl].send_add_player_packet(npc._id);
        }
        else {
            // 플레이어가 계속 보고 있음.
            clients[pl].send_move_packet(npc._id);
        }
    }

    for (auto pl : old_vl) {
        if (0 == new_vl.count(pl)) {
            clients[pl]._vl.lock();
            if (0 != clients[pl]._view_list.count(npc._id)) {
                clients[pl]._vl.unlock();
                clients[pl].send_remove_player_packet(npc._id);
            }
            else {
                clients[pl]._vl.unlock();
            }
        }
    }
}

void respawn_npc(int npc_id) {
    SESSION& npc = clients[npc_id];
    lock_guard<mutex> ll(npc._s_lock);
    npc.x = clients[npc_id].x;
	npc.y = clients[npc_id].y;
    npc.hp = 10;  // TODO: 변경 필요
    npc._state = ST_INGAME;
    npc._sector_x = npc.x / SECTOR_WIDTH;
    npc._sector_y = npc.y / SECTOR_HEIGHT;

    sector_locks[npc._sector_x][npc._sector_y].lock();
    g_sectors[npc._sector_x][npc._sector_y].insert(npc_id);
    sector_locks[npc._sector_x][npc._sector_y].unlock();

    unordered_set<int> vl;
    for (int y = max(npc._sector_y - 1, 0); y <= min(npc._sector_y + 1, SECTOR_ROWS - 1); ++y) {
        for (int x = max(npc._sector_x - 1, 0); x <= min(npc._sector_x + 1, SECTOR_COLS - 1); ++x) {
            lock_guard<mutex> lock(sector_locks[x][y]);
            for (int cl_id : g_sectors[x][y]) {
                if (clients[cl_id]._state != ST_INGAME) continue;
                if (true == is_npc(cl_id)) continue;
                if (can_see(npc_id, cl_id))
                    vl.insert(cl_id);
            }
        }
    }

    for (auto pl : vl) {
        clients[pl].send_add_player_packet(npc._id);
		WakeUpNPC(npc_id, pl);
    }
}

void worker_thread(HANDLE h_iocp) {
    while (true) {
        DWORD num_bytes;
        ULONG_PTR key;
        WSAOVERLAPPED* over = nullptr;
        BOOL ret = GetQueuedCompletionStatus(h_iocp, &num_bytes, &key, &over, INFINITE);
        OVER_EXP* ex_over = reinterpret_cast<OVER_EXP*>(over);
        if (FALSE == ret) {
            if (ex_over->_comp_type == OP_ACCEPT) cout << "Accept Error";
            else {
                cout << "GQCS Error on client[" << key << "]\n";
                disconnect(static_cast<int>(key));
                if (ex_over->_comp_type == OP_SEND) delete ex_over;
                continue;
            }
        }

        if ((0 == num_bytes) && ((ex_over->_comp_type == OP_RECV) || (ex_over->_comp_type == OP_SEND))) {
            disconnect(static_cast<int>(key));
            if (ex_over->_comp_type == OP_SEND) delete ex_over;
            continue;
        }

        switch (ex_over->_comp_type) {
        case OP_ACCEPT: {
            int client_id = get_new_client_id();
            if (client_id != -1) {
                {
                    lock_guard<mutex> ll(clients[client_id]._s_lock);
                    clients[client_id]._state = ST_ALLOC;
                }
                clients[client_id].x = 0;
                clients[client_id].y = 0;
                clients[client_id]._id = client_id;
                clients[client_id]._name[0] = 0;
                clients[client_id]._socket = g_c_socket;
                sprintf_s(clients[client_id]._name, "PLAYER%d", client_id);
                CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_c_socket), h_iocp, client_id, 0);
                clients[client_id].do_recv();
                g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
            }
            else {
                cout << "Max user exceeded.\n";
            }
            ZeroMemory(&g_a_over._over, sizeof(g_a_over._over));
            int addr_size = sizeof(SOCKADDR_IN);
            AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &g_a_over._over);
        }
		    break;

        case OP_RECV: {
            SESSION& sess = clients[key];
            sess._recv_buffer.insert(sess._recv_buffer.end(), ex_over->_send_buf, ex_over->_send_buf + num_bytes);

            while (!sess._recv_buffer.empty()) {
                // 패킷의 크기를 첫 바이트에서 가져옴 (단, unsigned char로 변환)
                unsigned short packet_size = static_cast<unsigned char>(sess._recv_buffer[0]);
                if (sess._recv_buffer.size() < packet_size)
                    break;
                process_packet(static_cast<int>(key), sess._recv_buffer.data());
                sess._recv_buffer.erase(sess._recv_buffer.begin(), sess._recv_buffer.begin() + packet_size);
            }
            sess.do_recv();
        }
            break;

        case OP_SEND:
            delete ex_over;
            break;

        case OP_NPC_MOVE: {
            bool keep_alive = false;
            for (int j = 0; j < MAX_USER; ++j) {
                if (clients[j]._state != ST_INGAME) continue;
                if (can_see(static_cast<int>(key), j)) {
                    keep_alive = true;
                    break;
                }
            }

            if(clients[key]._state != ST_INGAME)
				keep_alive = false;

            if (true == keep_alive) {
#ifdef DEBUG
				cout << "NPC[" << key << "] is alive, keep moving.\n";
#endif
                do_npc_random_move(static_cast<int>(key));
                TIMER_EVENT ev{ key, chrono::system_clock::now() + 1s, EV_RANDOM_MOVE, 0 };
                timer_queue.push(ev);
            }
            else {
                clients[key]._is_active = false;
            }
            delete ex_over;
        }
            break;

        case OP_RESPAWN: {
#ifdef DEBUG
			cout << "NPC[" << key << "] is respawned.\n";
#endif
            respawn_npc(static_cast<int>(key));
            delete ex_over;
        }
            break;

        case OP_TRACE: {
#ifdef DEBUG
			cout << "NPC[" << key << "] is tracing player[" << ex_over->_ai_target_obj << "]\n";
#endif
            int target_id = ex_over->_ai_target_obj;
            if (clients[target_id]._state != ST_INGAME) {
                clients[key]._is_active = false;
                delete ex_over;
                return;
            }

            vector<POINT> path = AStarPathfinding(clients[key].x, clients[key].y, clients[target_id].x, clients[target_id].y);
            if (!path.empty()) {
                POINT next_step = path.front();

                // 이동하려는 좌표의 충돌 여부 확인 (NPC 및 플레이어 모두 검사)
                bool collision = false;
                for (int i = 0; i < MAX_USER + MAX_NPC; ++i) {
                    if (i == key) continue;
                    if (clients[i]._state != ST_INGAME) continue;
                    if (clients[i].x == next_step.x && clients[i].y == next_step.y) {
                        collision = true;
                        break;
                    }
                }
                if (!collision) {
                    clients[key].x = next_step.x;
                    clients[key].y = next_step.y;
                }
                else {
                    // 움직일 수 없으면 추적 중단
                    clients[key]._is_active = false;
                    delete ex_over;
                    break;
                }

                if (is_attacked_from_player(target_id, key)) {
                    clients[target_id].hp -= clients[key].attack_damage;
                    if (clients[target_id].hp <= 0) {
                        clients[target_id].hp = clients[target_id].max_hp;
                        clients[target_id].exp = clients[target_id].exp / 2;
						clients[target_id].x = clients[target_id].start_x;
						clients[target_id].y = clients[target_id].start_y;
						clients[target_id].send_stat_change_packet();

                        int old_sector_x = clients[target_id]._sector_x;
                        int old_sector_y = clients[target_id]._sector_y;
                        int new_sector_x = clients[target_id].x / SECTOR_WIDTH;
                        int new_sector_y = clients[target_id].y / SECTOR_HEIGHT;

                        if (old_sector_x != new_sector_x || old_sector_y != new_sector_y) {
                            sector_locks[old_sector_x][old_sector_y].lock();
                            g_sectors[old_sector_x][old_sector_y].erase(target_id);
                            sector_locks[old_sector_x][old_sector_y].unlock();

                            sector_locks[new_sector_x][new_sector_y].lock();
                            g_sectors[new_sector_x][new_sector_y].insert(target_id);
                            sector_locks[new_sector_x][new_sector_y].unlock();

                            clients[target_id]._sector_x = new_sector_x;
                            clients[target_id]._sector_y = new_sector_y;
                        }

                        unordered_set<int> near_list;
                        clients[target_id]._vl.lock();
                        unordered_set<int> old_vlist = clients[target_id]._view_list;
                        clients[target_id]._vl.unlock();
                        for (int y = max(new_sector_y - 1, 0); y <= min(new_sector_y + 1, SECTOR_ROWS - 1); ++y) {
                            for (int x = max(new_sector_x - 1, 0); x <= min(new_sector_x + 1, SECTOR_COLS - 1); ++x) {
                                lock_guard<mutex> lock(sector_locks[x][y]);
                                for (int cl_id : g_sectors[x][y]) {
                                    if (clients[cl_id]._state != ST_INGAME) continue;
                                    if (cl_id == target_id) continue;
                                    if (can_see(target_id, cl_id))
                                        near_list.insert(cl_id);
                                }
                            }
                        }

                        clients[target_id].send_move_packet(target_id);

                        for (auto& pl : near_list) {
                            auto& cpl = clients[pl];
                            if (is_pc(pl)) {
                                cpl._vl.lock();
                                if (clients[pl]._view_list.count(target_id)) {
                                    cpl._vl.unlock();
                                    clients[pl].send_move_packet(target_id);
                                }
                                else {
                                    cpl._vl.unlock();
                                    clients[pl].send_add_player_packet(target_id);
                                }
                            }
                            else WakeUpNPC(pl, target_id);

                            if (old_vlist.count(pl) == 0)
                                clients[target_id].send_add_player_packet(pl);
                        }

                        for (auto& pl : old_vlist)
                            if (0 == near_list.count(pl)) {
                                clients[target_id].send_remove_player_packet(pl);
                                if (is_pc(pl))
                                    clients[pl].send_remove_player_packet(target_id);
                            }

						clients[key]._is_active = false;
                    }
                    clients[target_id].send_player_attack_packet(key);
                    clients[target_id].send_stat_change_packet();
                    clients[key]._is_active = false;
                }
                else {
                    for (int j = 0; j < MAX_USER; ++j) {
                        if (clients[j]._state != ST_INGAME) continue;
                        if (can_see(key, j)) {
                            clients[j].send_move_packet(key);
                        }
                    }

                    TIMER_EVENT ev{ key, chrono::system_clock::now() + 1s, EV_TRACE, target_id };
                    timer_queue.push(ev);
                }
            }
            else {
                clients[key]._is_active = false;
            }
            delete ex_over;
		}
			break;

            case OP_HEAL: {
				clients[key].hp += clients[key].max_hp / 10;
				if (clients[key].hp > clients[key].max_hp)
					clients[key].hp = clients[key].max_hp;

#ifdef DEBUG
				cout << "Client[" << key << "] is healed.\n";
#endif // DEBUG


				clients[key].send_stat_change_packet();

				TIMER_EVENT ev{ key, chrono::system_clock::now() + 5s, EV_HEAL, 0 };
				timer_queue.push(ev);
				delete ex_over;
            }
        }
    }
}

int API_get_x(lua_State* L) {
    int user_id =
        (int)lua_tointeger(L, -1);
    lua_pop(L, 2);
    int x = clients[user_id].x;
    lua_pushnumber(L, x);
    return 1;
}

int API_get_y(lua_State* L) {
    int user_id =
        (int)lua_tointeger(L, -1);
    lua_pop(L, 2);
    int y = clients[user_id].y;
    lua_pushnumber(L, y);
    return 1;
}

int API_SendMessage(lua_State* L) {
    int my_id = (int)lua_tointeger(L, -3);
    int user_id = (int)lua_tointeger(L, -2);
    char* mess = (char*)lua_tostring(L, -1);
    lua_pop(L, 4);

    clients[user_id].send_chat_packet(my_id, mess);
    return 0;
}

int API_SetDirection(lua_State* L) {
    int user_id = (int)lua_tointeger(L, -2);
    char dir = (char)lua_tointeger(L, -1);
    lua_pop(L, 3);
    clients[user_id]._ai_dir = dir;
    return 0;
}

void InitializeNPC() {
    cout << "NPC intialize begin.\n";
    for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i) {
        clients[i].x = rand() % W_WIDTH;
        clients[i].y = rand() % W_HEIGHT;
        clients[i]._id = i;
        clients[i]._state = ST_INGAME;
        clients[i]._sector_x = clients[i].x / SECTOR_WIDTH;
        clients[i]._sector_y = clients[i].y / SECTOR_HEIGHT;
        clients[i]._visual = static_cast<Visual>(rand() % 4 + 1);

        switch (clients[i]._visual) {
        case BALOONEER:
            // Peace, 고정
            clients[i].exp = 0;
            clients[i].max_exp = 0;
            clients[i].hp = 10;
            clients[i].max_hp = 10;
            clients[i].level = 5;
            clients[i].attack_damage = 5;
            break;
        case PINCERON:
            // Peace, 랜덤움직임
            clients[i].exp = 0;
            clients[i].max_exp = 0;
            clients[i].hp = 50;
            clients[i].max_hp = 50;
            clients[i].level = 7;
            clients[i].attack_damage = 7;
            break;
        case FROBBIT:
            // Aggro, 고정
            clients[i].exp = 0;
            clients[i].max_exp = 0;
            clients[i].hp = 100;
            clients[i].max_hp = 100;
            clients[i].level = 10;
            clients[i].attack_damage = 10;
            break;
        case HEDGEHAG:
            // Aggro, 고정
            clients[i].exp = 0;
            clients[i].max_exp = 0;
            clients[i].hp = 120;
            clients[i].max_hp = 120;
            clients[i].level = 12;
            clients[i].attack_damage = 12;
            break;
        }

        
        sprintf_s(clients[i]._name, "NPC%d", i);

        sector_locks[clients[i]._sector_x][clients[i]._sector_y].lock();
        g_sectors[clients[i]._sector_x][clients[i]._sector_y].insert(i);
        sector_locks[clients[i]._sector_x][clients[i]._sector_y].unlock();
    }
    cout << "NPC initialize end.\n";
}

void do_timer() {
    while (true) {
        TIMER_EVENT ev;
        OVER_EXP* ov;
        OVER_EXP* res_ov;
        auto current_time = chrono::system_clock::now();
        if (true == timer_queue.try_pop(ev)) {
            if (ev.wakeup_time > current_time) {
                timer_queue.push(ev);        // 최적화 필요
                this_thread::sleep_for(1ms);  // 실행시간이 아직 안되었으므로 잠시 대기
                continue;
            }
            switch (ev.event_id) {
            case EV_RANDOM_MOVE:
                ov = new OVER_EXP;
                ov->_comp_type = OP_NPC_MOVE;
                PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &ov->_over);
                break;
            case EV_RESPAWN:
                res_ov = new OVER_EXP;
                res_ov->_comp_type = OP_RESPAWN;
                PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &res_ov->_over);
                break;

            case EV_TRACE:
                ov = new OVER_EXP;
                ov->_comp_type = OP_TRACE;
                ov->_ai_target_obj = ev.target_id;
                PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &ov->_over);
                break;

			case EV_HEAL:
				res_ov = new OVER_EXP;
				res_ov->_comp_type = OP_HEAL;
				PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &res_ov->_over);
				break;
            }
            continue;        // 즉시 다음 작업 꺼내기
        }
        this_thread::sleep_for(1ms);   // timer_queue가 비어 있으니 잠시 기다렸다가 다시 시작
    }
}

void InitializeObstacles(const std::string& filename) {
    std::ifstream in_file(filename, std::ios::binary);

    if (!in_file) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return;
    }

    for (int i = 0; i < NUM_OBSTACLES; ++i) {
        in_file.read(reinterpret_cast<char*>(&obstacles[i].x), sizeof(int));
        in_file.read(reinterpret_cast<char*>(&obstacles[i].y), sizeof(int));

        if (!in_file) {
            std::cerr << "Error reading data from file: " << filename << std::endl;
            return;
        }

        int sector_x = obstacles[i].x / SECTOR_WIDTH;
        int sector_y = obstacles[i].y / SECTOR_HEIGHT;
        obstacle_grid[sector_x][sector_y].push_back(obstacles[i]);
    }

	cout << "Obstacles initialize end.\n";
}

int main() {
    WSADATA WSAData;
    WSAStartup(MAKEWORD(2, 2), &WSAData);
    g_s_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    SOCKADDR_IN server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT_NUM);
    server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
    bind(g_s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    listen(g_s_socket, SOMAXCONN);
    SOCKADDR_IN cl_addr;
    int addr_size = sizeof(cl_addr);

    InitializeNPC();
	InitializeObstacles("obstacles.bin");

    h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
    CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_s_socket), h_iocp, 9999, 0);
    g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    g_a_over._comp_type = OP_ACCEPT;
    AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &g_a_over._over);

    vector<thread> worker_threads;
    int num_threads = std::thread::hardware_concurrency();
    for (int i = 0; i < num_threads; ++i)
        worker_threads.emplace_back(worker_thread, h_iocp);
    thread timer_thread{ do_timer };
    timer_thread.join();
    for (auto& th : worker_threads)
        th.join();
    closesocket(g_s_socket);
    WSACleanup();
}
