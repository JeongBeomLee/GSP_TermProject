#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <unordered_map>
#include <Windows.h>
#include <chrono>
using namespace std;

#include "..\..\SERVER\SERVER\protocol.h"

sf::TcpSocket s_socket;

constexpr auto SCREEN_WIDTH = 16;
constexpr auto SCREEN_HEIGHT = 16;

constexpr auto TILE_WIDTH = 65;
constexpr auto WINDOW_WIDTH = SCREEN_WIDTH * TILE_WIDTH;   // size of window
constexpr auto WINDOW_HEIGHT = SCREEN_WIDTH * TILE_WIDTH;

constexpr int have_one_frame = 1;
constexpr int zero_frame_time = 0;

int g_left_x;
int g_top_y;
int g_myid;

sf::RenderWindow* g_window;
sf::Font g_font;
enum Direction { UP, DOWN, LEFT, RIGHT };
enum State { IDLE, WALK };
class OBJECT {
private:
	bool m_showing;
	sf::Sprite m_sprite;
	sf::Text m_name;
	sf::Text m_chat;
	chrono::system_clock::time_point m_message_end_time;

	// Animation
	bool m_have_animation;
	int m_frame_count;
	int m_current_frame;
	int m_frame_start_x;
	int m_frame_start_y;
	int m_frame_size_x;
	int m_frame_size_y;
	chrono::milliseconds m_frame_duration;
	chrono::system_clock::time_point m_last_frame_time;
	
public:
	int id;
	int m_x, m_y;
	char name[NAME_SIZE];
	State m_state;
	Direction m_direction;

	OBJECT(sf::Texture& t, int x, int y, int x2, int y2, 
		   int frame_start_x, int frame_start_y,
		   int frame_size_x, int frame_size_y, 
		   int frame_count, int frame_duration_ms) {
		m_have_animation = true;
		m_showing = false;

		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
		m_frame_count = frame_count;
		m_current_frame = 0;
		m_frame_duration = chrono::milliseconds(frame_duration_ms);
		m_state = IDLE;
		m_direction = DOWN;
		m_frame_start_x = frame_start_x;
		m_frame_start_y = frame_start_y;
		m_frame_size_x = frame_size_x;
		m_frame_size_y = frame_size_y;

		set_name("NONAME");
		m_message_end_time = chrono::system_clock::now();
		m_last_frame_time = chrono::system_clock::now();
	}

	OBJECT(sf::Texture& t, int x, int y, int x2, int y2) {
		m_have_animation = false;
		m_showing = false;
		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
		set_name("NONAME");
		m_message_end_time = chrono::system_clock::now();
		m_frame_size_x = 0;
		m_frame_size_y = 0;
	}

	OBJECT() {
		m_showing = false;
	}

	void show() {
		m_showing = true;
	}

	void hide() {
		m_showing = false;
	}

	void a_move(int x, int y) {
		m_sprite.setPosition((float)x, (float)y);
	}

	void a_draw() {
		g_window->draw(m_sprite);
	}

	void move(int x, int y) {
		m_x = x;
		m_y = y;

		if (m_have_animation)
			set_state(WALK);
	}

	void draw() {
		if (false == m_showing) 
			return;

		if (m_have_animation)
			update_animation();

		float rx = (m_x - g_left_x) * 65.0f + 1;
		if (m_direction == LEFT)
			rx = (m_x - g_left_x) * 65.0f + 65.0f;
		float ry = (m_y - g_top_y) * 65.0f + 1;
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);

		auto size = m_name.getGlobalBounds();
		if (m_message_end_time < chrono::system_clock::now()) {
			m_name.setPosition(rx + 32 - size.width / 2, ry - 32);
			if (m_direction == LEFT)
				m_name.setPosition(rx - 32 - size.width / 2, ry - 32);
			g_window->draw(m_name);
		}
		else {
			m_chat.setPosition(rx + 32 - size.width / 2, ry - 32);
			if (m_direction == LEFT)
				m_chat.setPosition(rx - 32 - size.width / 2, ry - 32);
			g_window->draw(m_chat);
		}
	}

	void set_name(const char str[]) {
		m_name.setFont(g_font);
		m_name.setString(str);
		if (id < MAX_USER) 
			m_name.setFillColor(sf::Color(255, 255, 255));
		else 
			m_name.setFillColor(sf::Color(255, 255, 0));
		m_name.setStyle(sf::Text::Bold);
	}

	void set_chat(const char str[]) {
		m_chat.setFont(g_font);
		m_chat.setString(str);
		m_chat.setFillColor(sf::Color(255, 255, 255));
		m_chat.setStyle(sf::Text::Bold);
		m_message_end_time = chrono::system_clock::now() + chrono::seconds(3);
	}

	void update_animation() {
		update_direction();

		if (m_frame_count == 1) {
			sf::Vector2i frame_start(m_frame_start_x + m_frame_size_x, m_frame_start_y);
			sf::Vector2i frame_size(m_frame_size_x, m_frame_size_y);
			m_sprite.setTextureRect(sf::IntRect(frame_start, frame_size));

			if(m_direction == LEFT)
				m_sprite.setScale(-(static_cast<float>(TILE_WIDTH) / m_frame_size_x), TILE_WIDTH / m_frame_size_y);
			else
				m_sprite.setScale(TILE_WIDTH / m_frame_size_x, TILE_WIDTH / m_frame_size_y);
		}
		else {
			auto now = chrono::system_clock::now();
			if (now - m_last_frame_time > m_frame_duration) {
				m_current_frame = (m_current_frame + 1) % m_frame_count;

				sf::Vector2i frame_start(m_frame_start_x + m_current_frame * m_frame_size_x, m_frame_start_y);
				sf::Vector2i frame_size(m_frame_size_x, m_frame_size_y);
				m_sprite.setTextureRect(sf::IntRect(frame_start, frame_size));
				if (m_direction == LEFT)
					m_sprite.setScale(-(static_cast<float>(TILE_WIDTH) / m_frame_size_x), TILE_WIDTH / m_frame_size_y);
				else
					m_sprite.setScale(TILE_WIDTH / m_frame_size_x, TILE_WIDTH / m_frame_size_y);
				m_last_frame_time = now;
			}
		}
	}

	void set_state(State new_state) {
		if (m_state != new_state) {
			m_state = new_state;
			switch (new_state) {
			case IDLE:
				m_frame_count = 1;
				m_frame_duration = chrono::milliseconds(100);
				m_current_frame = 0;
				break;
			case WALK:
				m_frame_count = 3;
				m_frame_duration = chrono::milliseconds(200);
				m_current_frame = 0;
				break;
			}
		}
	}

	void update_direction() {
		switch (m_state) {
		case IDLE:
			switch (m_direction) {
			case UP:
				m_frame_start_x = 2;
				m_frame_start_y = 2;
				break;
			case DOWN:
				m_frame_start_x = 2;
				m_frame_start_y = 2 + (32 * 4);
				break;
			case LEFT:
				m_frame_start_x = 2;
				m_frame_start_y = 2 + (32 * 2);
				break;
			case RIGHT:
				m_frame_start_x = 2;
				m_frame_start_y = 2 + (32 * 2);
				break;
			}
			break;
		case WALK:
			switch (m_direction) {
			case UP:
				m_frame_start_x = 2;
				m_frame_start_y = 2;
				break;
			case DOWN:
				m_frame_start_x = 2;
				m_frame_start_y = 2 + (32 * 4);
				break;
			case LEFT:
				m_frame_start_x = 2;
				m_frame_start_y = 2 + (32 * 2);
				break;
			case RIGHT:
				m_frame_start_x = 2;
				m_frame_start_y = 2 + (32 * 2);
				break;
			}
			break;
		}
	}

	void stop() {
		if (m_have_animation)
			set_state(IDLE);
	}
};

OBJECT avatar;
unordered_map <int, OBJECT> players;

OBJECT white_tile;
OBJECT black_tile;

sf::Texture* board;
sf::Texture* pieces;
sf::Texture* test_avatar;

void client_initialize()
{
	board = new sf::Texture;
	pieces = new sf::Texture;
	test_avatar = new sf::Texture;

	board->loadFromFile("resources/chessmap.bmp");
	pieces->loadFromFile("resources/chess2.png");
	test_avatar->loadFromFile("resources/Lea.png");
	if (false == g_font.loadFromFile("cour.ttf")) {
		cout << "Font Loading Error!\n";
		exit(-1);
	}
	white_tile = OBJECT{ *board, 5, 5, TILE_WIDTH, TILE_WIDTH };
	black_tile = OBJECT{ *board, 69, 5, TILE_WIDTH, TILE_WIDTH };
	avatar = OBJECT{ *test_avatar, 2, 2, 32, 32, 2, 2, 32, 32, have_one_frame, zero_frame_time };
	//avatar.move(4, 4);
}

void client_finish()
{
	players.clear();
	delete board;
	delete pieces;
}

void ProcessPacket(char* ptr)
{
	static bool first_time = true;
	switch (ptr[1])
	{
	case SC_LOGIN_INFO:
	{
		SC_LOGIN_INFO_PACKET * packet = reinterpret_cast<SC_LOGIN_INFO_PACKET*>(ptr);
		g_myid = packet->id;
		avatar.id = g_myid;
		avatar.move(packet->x, packet->y);
		g_left_x = packet->x - SCREEN_WIDTH / 2;
		g_top_y = packet->y - SCREEN_HEIGHT / 2;
		avatar.show();
	}
	break;

	case SC_ADD_OBJECT:
	{
		SC_ADD_OBJECT_PACKET* my_packet = reinterpret_cast<SC_ADD_OBJECT_PACKET*>(ptr);
		int id = my_packet->id;

		if (id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - SCREEN_WIDTH / 2;
			g_top_y = my_packet->y - SCREEN_HEIGHT / 2;
			avatar.show();
		}
		else if (id < MAX_USER) {
			players[id] = OBJECT{ *pieces, 0, 0, 64, 64 };
			players[id].id = id;
			players[id].move(my_packet->x, my_packet->y);
			players[id].set_name(my_packet->name);
			players[id].show();
		}
		else {
			players[id] = OBJECT{ *pieces, 256, 0, 64, 64 };
			players[id].id = id;
			players[id].move(my_packet->x, my_packet->y);
			players[id].set_name(my_packet->name);
			players[id].show();
		}
		break;
	}
	case SC_MOVE_OBJECT:
	{
		SC_MOVE_OBJECT_PACKET* my_packet = reinterpret_cast<SC_MOVE_OBJECT_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - SCREEN_WIDTH/2;
			g_top_y = my_packet->y - SCREEN_HEIGHT/2;
		}
		else {
			players[other_id].move(my_packet->x, my_packet->y);
		}
		break;
	}

	case SC_REMOVE_OBJECT:
	{
		SC_REMOVE_OBJECT_PACKET* my_packet = reinterpret_cast<SC_REMOVE_OBJECT_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.hide();
		}
		else {
			players.erase(other_id);
		}
		break;
	}
	case SC_CHAT:
	{
		SC_CHAT_PACKET* my_packet = reinterpret_cast<SC_CHAT_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.set_chat(my_packet->mess);
		}
		else {
			players[other_id].set_chat(my_packet->mess);
		}

		break;
	}
	default:
		printf("Unknown PACKET type [%d]\n", ptr[1]);
	}
}

void process_data(char* net_buf, size_t io_byte)
{
	char* ptr = net_buf;
	static size_t in_packet_size = 0;
	static size_t saved_packet_size = 0;
	static char packet_buffer[BUF_SIZE];

	while (0 != io_byte) {
		if (0 == in_packet_size) in_packet_size = ptr[0];
		if (io_byte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			ProcessPacket(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			io_byte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
			saved_packet_size += io_byte;
			io_byte = 0;
		}
	}
}

void client_main()
{
	char net_buf[BUF_SIZE];
	size_t	received;

	auto recv_result = s_socket.receive(net_buf, BUF_SIZE, received);
	if (recv_result == sf::Socket::Error)
	{
		wcout << L"Recv 에러!";
		exit(-1);
	}
	if (recv_result == sf::Socket::Disconnected) {
		wcout << L"Disconnected\n";
		exit(-1);
	}
	if (recv_result != sf::Socket::NotReady)
		if (received > 0) process_data(net_buf, received);

	for (int i = 0; i < SCREEN_WIDTH; ++i)
		for (int j = 0; j < SCREEN_HEIGHT; ++j) {
			int tile_x = i + g_left_x;
			int tile_y = j + g_top_y;
			if ((tile_x < 0) || (tile_y < 0)) continue;
			if (0 == (tile_x / 2 + tile_y / 2) % 2) {
				white_tile.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
				white_tile.a_draw();
			}
			else {
				black_tile.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
				black_tile.a_draw();
			}
		}
	
	avatar.draw();
	for (auto& pl : players) 
		pl.second.draw();
	
	sf::Text text;
	text.setFont(g_font);
	char buf[100];
	sprintf_s(buf, "(%d, %d)", avatar.m_x, avatar.m_y);
	text.setString(buf);
	g_window->draw(text);
}

void send_packet(void *packet)
{
	unsigned char *p = reinterpret_cast<unsigned char *>(packet);
	size_t sent = 0;
	s_socket.send(packet, p[0], sent);
}

int main()
{
	wcout.imbue(locale("korean"));

	sf::Socket::Status status = s_socket.connect("127.0.0.1", PORT_NUM);
	//sf::Socket::Status status = s_socket.connect(ip, PORT_NUM);

	s_socket.setBlocking(false);

	if (status != sf::Socket::Done) {
		wcout << L"서버와 연결할 수 없습니다.\n";
		exit(-1);
	}

	client_initialize();
	CS_LOGIN_PACKET p;
	p.size = sizeof(p);
	p.type = CS_LOGIN;

	string player_name{ "P" };
	player_name += to_string(GetCurrentProcessId());
	
	strcpy_s(p.name, player_name.c_str());
	send_packet(&p);
	avatar.set_name(p.name);

	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "2D CLIENT");
	g_window = &window;

	while (window.isOpen())
	{
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				window.close();
			if (event.type == sf::Event::KeyPressed) {
				int direction = -1;
				switch (event.key.code) {
				case sf::Keyboard::Left:
					direction = 2;
					avatar.m_direction = LEFT;
					break;
				case sf::Keyboard::Right:
					direction = 3;
					avatar.m_direction = RIGHT;
					break;
				case sf::Keyboard::Up:
					direction = 0;
					avatar.m_direction = UP;
					break;
				case sf::Keyboard::Down:
					direction = 1;
					avatar.m_direction = DOWN;
					break;
				case sf::Keyboard::Escape:
					window.close();
					break;
				}
				if (-1 != direction) {
					CS_MOVE_PACKET p;
					p.size = sizeof(p);
					p.type = CS_MOVE;
					p.direction = direction;
					send_packet(&p);
				}
			}

			if (event.type == sf::Event::KeyReleased) {
				int direction = -1;
				switch (event.key.code) {
				case sf::Keyboard::Left:
					direction = 2;
					avatar.m_direction = LEFT;
					avatar.stop();
					break;
				case sf::Keyboard::Right:
					direction = 3;
					avatar.m_direction = RIGHT;
					avatar.stop();
					break;
				case sf::Keyboard::Up:
					direction = 0;
					avatar.m_direction = UP;
					avatar.stop();
					break;
				case sf::Keyboard::Down:
					direction = 1;
					avatar.m_direction = DOWN;
					avatar.stop();
					break;
				case sf::Keyboard::Escape:
					window.close();
					break;
				}
			}
		}

		window.clear();
		client_main();
		window.display();
	}
	client_finish();

	return 0;
}