#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <array>
#include <fstream>
#include <unordered_map>
#include <Windows.h>
#include <chrono>
using namespace std;

#include "..\..\SERVER\SERVER\protocol.h"
#include <deque>

sf::TcpSocket s_socket;

constexpr auto SCREEN_WIDTH = 16;
constexpr auto SCREEN_HEIGHT = 16;

constexpr auto TILE_WIDTH = 65;
constexpr auto WINDOW_WIDTH = SCREEN_WIDTH * TILE_WIDTH;   // size of window
constexpr auto WINDOW_HEIGHT = SCREEN_WIDTH * TILE_WIDTH;

int g_left_x;
int g_top_y;
int g_myid;
array<POINT, NUM_OBSTACLES> obstacles;

sf::Texture* UIboard;
sf::Texture* LeaTexture;
sf::Texture* BalooneerTexture;
sf::Texture* PinceronTexture;
sf::Texture* FrobbitTexture;
sf::Texture* HedgehagTexture;
sf::Texture* PentafistTexture;
sf::Texture* ChatBoardTexture;
sf::Texture* ObstacleTexture;
sf::Sprite ChatBoardSprite;
sf::Sprite ObstacleSprite;

constexpr int Visual_Dir_Up_Start_x		= 0;
constexpr int Visual_Dir_Right_Start_x	= 64;
constexpr int Visual_Dir_Down_Start_x	= 128;
constexpr int Visual_Dir_Left_Start_x	= 192;

sf::RenderWindow* g_window;
sf::Font g_font;
enum Direction { UP, DOWN, LEFT, RIGHT, COUNT };

bool is_pc(int object_id)
{
	return object_id < MAX_USER;
}

bool is_npc(int object_id)
{
	return !is_pc(object_id);
}

class OBJECT {
private:
	bool m_showing;
	bool m_attacking;
	sf::Sprite m_sprite;
	sf::Text m_name;
	sf::Text m_chat;
	chrono::system_clock::time_point m_message_end_time;
	chrono::system_clock::time_point m_attack_end_time;

public:
	int id;
	int exp, max_exp;
	int hp, max_hp;
	int level;
	int m_x, m_y;
	Visual m_visual;
	char name[NAME_SIZE];
	Direction m_direction;
	array<sf::Sprite, Direction::COUNT> m_fists;

	OBJECT(sf::Texture& t, int x, int y, int x2, int y2) {
		m_fists[UP].setTexture(*PentafistTexture);
		m_fists[UP].setTextureRect(sf::IntRect(0, 0, 64, 64));

		m_fists[DOWN].setTexture(*PentafistTexture);
		m_fists[DOWN].setTextureRect(sf::IntRect(0, 0, 64, 64));

		m_fists[LEFT].setTexture(*PentafistTexture);
		m_fists[LEFT].setTextureRect(sf::IntRect(0, 0, 64, 64));

		m_fists[RIGHT].setTexture(*PentafistTexture);
		m_fists[RIGHT].setTextureRect(sf::IntRect(0, 0, 64, 64));

		m_showing = false;
		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
		set_name("NONAME");
		m_message_end_time = chrono::system_clock::now();
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
	}

	void draw() {
		if (false == m_showing) 
			return;

		switch (m_direction) {
		case UP:
			m_sprite.setTextureRect(sf::IntRect(Visual_Dir_Up_Start_x, 0, 64, 64));
			break;
		case DOWN:
			m_sprite.setTextureRect(sf::IntRect(Visual_Dir_Down_Start_x, 0, 64, 64));
			break;
		case LEFT:
			m_sprite.setTextureRect(sf::IntRect(Visual_Dir_Left_Start_x, 0, 64, 64));
			break;
		case RIGHT:
			m_sprite.setTextureRect(sf::IntRect(Visual_Dir_Right_Start_x, 0, 64, 64));
			break;
		default:
			break;
		}

		float rx = (m_x - g_left_x) * 65.0f + 1;
		float ry = (m_y - g_top_y) * 65.0f + 1;
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);

		auto size = m_name.getGlobalBounds();
		if (m_message_end_time < chrono::system_clock::now()) {
			m_name.setPosition(rx + 32 - size.width / 2, ry - 32);
			g_window->draw(m_name);
		}
		else {
			m_chat.setPosition(rx + 32 - size.width / 2, ry - 32);
			g_window->draw(m_chat);
		}

		if (m_attacking && chrono::system_clock::now() > m_attack_end_time) {
			m_attacking = false;
		}
		else if (m_attacking) {
			for (int i = 0; i < Direction::COUNT; ++i) {
				switch (i) {
				case UP:
					m_fists[i].setPosition(rx, ry - 65);
					break;
				case DOWN:
					m_fists[i].setPosition(rx, ry + 65);
					break;
				case LEFT:
					m_fists[i].setPosition(rx - 65, ry);
					break;
				case RIGHT:
					m_fists[i].setPosition(rx + 65, ry);
					break;
				}
				g_window->draw(m_fists[i]);
			}
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

		strcpy_s(name, str);
	}

	void set_chat(const char str[]) {
		m_chat.setFont(g_font);
		m_chat.setString(str);
		m_chat.setFillColor(sf::Color(255, 255, 255));
		m_chat.setStyle(sf::Text::Bold);
		m_message_end_time = chrono::system_clock::now() + chrono::seconds(3);
	}

	void update_direction(int x, int y) {
		if (m_x < x)		m_direction = RIGHT;
		else if (m_x > x)	m_direction = LEFT;
		else if (m_y < y)	m_direction = DOWN;
		else if (m_y > y)	m_direction = UP;
	}

	void attack() {
		m_attacking = true;
		m_attack_end_time = chrono::system_clock::now() + chrono::seconds(1);
	}
};

OBJECT myPlayer;
unordered_map <int, OBJECT> players;

OBJECT UI_board;
OBJECT bright_grass;
OBJECT dark_grass;

sf::Texture* board;
chrono::system_clock::time_point last_move_time;
chrono::system_clock::time_point last_attack_time;
sf::Text chat_log_text;
deque<std::string> chat_log;

void client_initialize()
{
	UIboard				= new sf::Texture;
	board				= new sf::Texture;
	LeaTexture			= new sf::Texture;
	BalooneerTexture	= new sf::Texture;
	PinceronTexture		= new sf::Texture;
	PentafistTexture	= new sf::Texture;
	ChatBoardTexture	= new sf::Texture;
	ObstacleTexture		= new sf::Texture;
	FrobbitTexture		= new sf::Texture;
	HedgehagTexture		= new sf::Texture;

	UIboard->loadFromFile("resources/UI.png");
	board->loadFromFile("resources/chessmap.bmp");
	LeaTexture->loadFromFile("resources/Lea.png");
	BalooneerTexture->loadFromFile("resources/Balooneer.png");
	PinceronTexture->loadFromFile("resources/Pinceron.png");
	FrobbitTexture->loadFromFile("resources/Frobbit.png");
	HedgehagTexture->loadFromFile("resources/Hedgehag.png");
	PentafistTexture->loadFromFile("resources/Pentafist.png");
	ChatBoardTexture->loadFromFile("resources/chat_board.png");
	ChatBoardSprite.setTexture(*ChatBoardTexture);
	ObstacleTexture->loadFromFile("resources/obstacle.png");
	ObstacleSprite.setTexture(*ObstacleTexture);
	if (false == g_font.loadFromFile("cour.ttf")) {
		cout << "Font Loading Error!\n";
		exit(-1);
	}
	ChatBoardSprite.setTextureRect(sf::IntRect(0, 0, 900, 26));
	ChatBoardSprite.setPosition(70, 945);
	ObstacleSprite.setTextureRect(sf::IntRect(0, 0, 64, 64));

	UI_board = OBJECT{ *UIboard, 0, 0, 1040, 1040 };
	bright_grass = OBJECT{ *board, 5, 5, TILE_WIDTH, TILE_WIDTH };
	dark_grass = OBJECT{ *board, 69, 5, TILE_WIDTH, TILE_WIDTH };
	myPlayer = OBJECT{ *LeaTexture, 0, 0, 64, 64 };
	myPlayer.move(4, 4);

	chat_log_text.setFont(g_font);
	chat_log_text.setCharacterSize(20);
	chat_log_text.setFillColor(sf::Color::White);

	last_move_time = chrono::system_clock::now() - chrono::seconds(1);
}

void client_finish()
{
	players.clear();
	delete board;
	delete LeaTexture;
	delete BalooneerTexture;
	delete PinceronTexture;
	delete PentafistTexture;
}

void ProcessPacket(char* ptr)
{
	static bool first_time = true;
	switch (ptr[2]) {
	case SC_LOGIN_INFO: {	// 데베
		SC_LOGIN_INFO_PACKET * packet = reinterpret_cast<SC_LOGIN_INFO_PACKET*>(ptr);

		g_myid				= packet->id;
		myPlayer.id			= g_myid;
		myPlayer.hp			= packet->hp;
		myPlayer.max_hp		= packet->max_hp;
		myPlayer.exp		= packet->exp;
		myPlayer.max_exp	= 100;
		myPlayer.level		= packet->level;
		myPlayer.m_visual	= static_cast<Visual>(packet->visual);
		myPlayer.move(packet->x, packet->y);

		g_left_x	= packet->x - SCREEN_WIDTH / 2;
		g_top_y		= packet->y - SCREEN_HEIGHT / 2;
		myPlayer.show();
	}
	break;

	case SC_ADD_OBJECT:
	{
		SC_ADD_OBJECT_PACKET* my_packet = reinterpret_cast<SC_ADD_OBJECT_PACKET*>(ptr);
		int id = my_packet->id;

		if (id == g_myid) {
			myPlayer.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - SCREEN_WIDTH / 2;
			g_top_y = my_packet->y - SCREEN_HEIGHT / 2;
			myPlayer.show();
		}
		else if (id < MAX_USER) {
			players[id] = OBJECT{ *LeaTexture, 0, 0, 64, 64 };
			players[id].id = id;
			players[id].m_visual = static_cast<Visual>(my_packet->visual);
			players[id].move(my_packet->x, my_packet->y);
			players[id].set_name(my_packet->name);
			players[id].show();
		}
		else {
			if (my_packet->visual == static_cast<int>(Visual::BALOONEER)) {
				players[id] = OBJECT{ *BalooneerTexture, 0, 0, 64, 64 };
				players[id].m_fists[UP].setColor(sf::Color(255, 0, 0, 128));
				players[id].m_fists[DOWN].setColor(sf::Color(255, 0, 0, 128));
				players[id].m_fists[LEFT].setColor(sf::Color(255, 0, 0, 128));
				players[id].m_fists[RIGHT].setColor(sf::Color(255, 0, 0, 128));
			}
			else if (my_packet->visual == static_cast<int>(Visual::PINCERON)) {
				players[id] = OBJECT{ *PinceronTexture, 0, 0, 64, 64 };
				players[id].m_fists[UP].setColor(sf::Color(255, 0, 0, 128));
				players[id].m_fists[DOWN].setColor(sf::Color(255, 0, 0, 128));
				players[id].m_fists[LEFT].setColor(sf::Color(255, 0, 0, 128));
				players[id].m_fists[RIGHT].setColor(sf::Color(255, 0, 0, 128));
			}
			else if (my_packet->visual == static_cast<int>(Visual::FROBBIT)) {
				players[id] = OBJECT{ *FrobbitTexture, 0, 0, 64, 64 };
				players[id].m_fists[UP].setColor(sf::Color(255, 0, 0, 128));
				players[id].m_fists[DOWN].setColor(sf::Color(255, 0, 0, 128));
				players[id].m_fists[LEFT].setColor(sf::Color(255, 0, 0, 128));
				players[id].m_fists[RIGHT].setColor(sf::Color(255, 0, 0, 128));
			}
			else if (my_packet->visual == static_cast<int>(Visual::HEDGEHAG)) {
				players[id] = OBJECT{ *HedgehagTexture, 0, 0, 64, 64 };
				players[id].m_fists[UP].setColor(sf::Color(255, 0, 0, 128));
				players[id].m_fists[DOWN].setColor(sf::Color(255, 0, 0, 128));
				players[id].m_fists[LEFT].setColor(sf::Color(255, 0, 0, 128));
				players[id].m_fists[RIGHT].setColor(sf::Color(255, 0, 0, 128));
			}
			players[id].id = id;
			players[id].m_visual = static_cast<Visual>(my_packet->visual);
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
			myPlayer.update_direction(my_packet->x, my_packet->y);
			myPlayer.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - SCREEN_WIDTH / 2;
			g_top_y = my_packet->y - SCREEN_HEIGHT / 2;
		}
		else {
			players[other_id].update_direction(my_packet->x, my_packet->y);
			players[other_id].move(my_packet->x, my_packet->y);
		}
		break;
	}

	case SC_REMOVE_OBJECT:
	{
		SC_REMOVE_OBJECT_PACKET* my_packet = reinterpret_cast<SC_REMOVE_OBJECT_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			myPlayer.hide();
		}
		else {
			players.erase(other_id);
		}
		break;
	}
	case SC_CHAT:
	{
		SC_CHAT_PACKET* my_packet = reinterpret_cast<SC_CHAT_PACKET*>(ptr);
		string chat_str;
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			myPlayer.set_chat(my_packet->mess);
			chat_str = myPlayer.name + string(": ") + my_packet->mess;
		}
		else {
			if (is_pc(other_id)) {
				players[other_id].set_chat(my_packet->mess);
				chat_str = players[other_id].name + string(": ") + my_packet->mess;
			}
		}

		// 채팅 로그에 추가
		chat_log.push_front(chat_str);
		if (chat_log.size() > 4) {
			chat_log.pop_back();
		}
		break;
	}
	case SC_PLAYER_ATTACK: {
		SC_PLAYER_ATTACK_PACKET* my_packet = reinterpret_cast<SC_PLAYER_ATTACK_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id != g_myid) {
			players[other_id].attack();
		}
		break;
	}
	case SC_STAT_CHANGE: {
		SC_STAT_CHANGE_PACKET* my_packet = reinterpret_cast<SC_STAT_CHANGE_PACKET*>(ptr);

		if (myPlayer.exp < my_packet->exp) {
			int dist = my_packet->exp - myPlayer.exp;

			chat_log.push_front("Get " + to_string(dist) + " EXP!");
			if (chat_log.size() > 4) {
				chat_log.pop_back();
			}
		}
		
		if (myPlayer.level < my_packet->level) {
			myPlayer.max_exp = myPlayer.max_exp * (2 * (my_packet->level - myPlayer.level));
			chat_log.push_front("Level Up!");
			if (chat_log.size() > 4) {
				chat_log.pop_back();
			}
		}

		if (myPlayer.hp > my_packet->hp) {
			int dist = myPlayer.hp - my_packet->hp;
			cout << "Get " << dist << " Damage!\n";
		}

		myPlayer.hp = my_packet->hp;
		myPlayer.max_hp = my_packet->max_hp;
		myPlayer.exp = my_packet->exp;
		myPlayer.level = my_packet->level;
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
		if (0 == in_packet_size) 
			in_packet_size = static_cast<unsigned short>(*ptr);
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

bool chat_mode = false;
sf::String chat_input;

void client_main()
{
	char net_buf[BUF_SIZE];
	size_t	received;
	auto recv_result = s_socket.receive(net_buf, BUF_SIZE, received);
	if (recv_result == sf::Socket::Error) {
		wcout << L"Recv 에러!";
		exit(-1);
	}

	if (recv_result == sf::Socket::Disconnected) {
		wcout << L"Disconnected\n";
		exit(-1);
	}

	if (recv_result != sf::Socket::NotReady)
		if (received > 0) 
			process_data(net_buf, received);

	sf::Color color = sf::Color(135, 206, 235);
	g_window->clear(color);

	for (int i = 0; i < SCREEN_WIDTH; ++i)
		for (int j = 0; j < SCREEN_HEIGHT; ++j) {
			int tile_x = i + g_left_x;
			int tile_y = j + g_top_y;
			if ((tile_x < 0) || (tile_y < 0)) continue;
			if (0 == (tile_x / 2 + tile_y / 2) % 2) {
				bright_grass.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
				bright_grass.a_draw();
			}
			else {
				dark_grass.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
				dark_grass.a_draw();
			}
		}

	// Draw obstacles
	for (const auto& obstacle : obstacles) {
		int rel_x = obstacle.x - myPlayer.m_x;
		int rel_y = obstacle.y - myPlayer.m_y;

		if (rel_x >= -7 && rel_x <= 6 && rel_y >= -6 && rel_y <= 6) {
			float rx = (obstacle.x - g_left_x) * 65.0f + 1;
			float ry = (obstacle.y - g_top_y) * 65.0f + 1;
			ObstacleSprite.setPosition(rx, ry);
			g_window->draw(ObstacleSprite);
		}
	}
	
	myPlayer.draw();
	for (auto& pl : players) 
		pl.second.draw();

	UI_board.a_draw();
	
	sf::Text text;
	text.setFont(g_font);
	char buf[100];
	sprintf_s(buf, "(%d, %d)", myPlayer.m_x, myPlayer.m_y);
	text.setString(buf);
	text.setOutlineThickness(3);
	text.setPosition(65, 130);

	g_window->draw(text);

	sf::Text LevelText;
	LevelText.setFont(g_font);
	LevelText.setFillColor(sf::Color(138, 219, 133));
	LevelText.setOutlineColor(sf::Color(138, 219, 133));
	LevelText.setCharacterSize(25);
	LevelText.setOutlineThickness(1);
	LevelText.setPosition(160, 78);
	LevelText.setString(to_string(myPlayer.level));
	g_window->draw(LevelText);


	sf::Text HPText;
	HPText.setFont(g_font);
	HPText.setFillColor(sf::Color(255, 0, 0));
	HPText.setOutlineColor(sf::Color(255, 0, 0));
	HPText.setCharacterSize(25);
	HPText.setOutlineThickness(1);
	HPText.setPosition(365, 78);
	HPText.setString(to_string(myPlayer.hp) + " / " + to_string(myPlayer.max_hp));
	g_window->draw(HPText);


	sf::Text EXPText;
	EXPText.setFont(g_font);
	EXPText.setFillColor(sf::Color(138, 219, 133));
	EXPText.setOutlineColor(sf::Color(138, 219, 133));
	EXPText.setCharacterSize(25);
	EXPText.setOutlineThickness(1);
	EXPText.setPosition(636, 78);
	EXPText.setString(to_string(myPlayer.exp) + " / " + to_string(myPlayer.max_exp));
	g_window->draw(EXPText);

	for (int i = 0; i < chat_log.size(); ++i) {
		chat_log_text.setString(chat_log[i]);
		chat_log_text.setPosition(80, 910 - (i * 20));
		g_window->draw(chat_log_text);
	}

	if (chat_mode) {
		g_window->draw(ChatBoardSprite);
		sf::Text chat_cloud;
		chat_cloud.setFont(g_font);
		chat_cloud.setFillColor(sf::Color::Black);
		chat_cloud.setCharacterSize(20);
		chat_cloud.setPosition(70, 945);
		chat_cloud.setString("< ");
		g_window->draw(chat_cloud);

		sf::Text chat_text;
		chat_text.setFont(g_font);
		chat_text.setFillColor(sf::Color::Black);
		chat_text.setCharacterSize(20);
		chat_text.setPosition(90, 945);
		chat_text.setString(chat_input);
		g_window->draw(chat_text);
	}
}

void send_packet(void *packet)
{
	unsigned char *p = reinterpret_cast<unsigned char *>(packet);
	size_t sent = 0;
	s_socket.send(packet, p[0], sent);
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
	}

	cout << "Obstacles initialize end.\n";
}

int main()
{
	wcout.imbue(locale("korean"));

	//sf::Socket::Status status = s_socket.connect("127.0.0.1", PORT_NUM);
	sf::IpAddress ip;
	cout << "Input Server IP: ";
	cin >> ip;
	sf::Socket::Status status = s_socket.connect(ip, PORT_NUM);

	s_socket.setBlocking(false);

	if (status != sf::Socket::Done) {
		wcout << L"서버와 연결할 수 없습니다.\n";
		exit(-1);
	}

	InitializeObstacles("obstacles.bin");
	client_initialize();
	// 데베
	CS_LOGIN_PACKET p;
	p.size = sizeof(p);
	p.type = CS_LOGIN;
	string player_name{ "P" };
	player_name += to_string(GetCurrentProcessId());
	strcpy_s(p.name, player_name.c_str());
	send_packet(&p);
	
	myPlayer.set_name(p.name);

	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "2D CLIENT");
	g_window = &window;

	while (window.isOpen()) {
		sf::Event event;
		while (window.pollEvent(event)) {
			if (event.type == sf::Event::Closed)
				window.close();
			if (event.type == sf::Event::KeyPressed) {
				auto now = chrono::system_clock::now();
				auto duration = chrono::duration_cast<chrono::seconds>(now - last_move_time).count();
				auto attack_duration = chrono::duration_cast<chrono::seconds>(now - last_attack_time).count();
				if (event.key.code == sf::Keyboard::Enter) {
					if (chat_mode) {
						CS_CHAT_PACKET p;
						p.size = sizeof(p);
						p.type = CS_CHAT;

						size_t convertedChars = 0;
						wcstombs_s(&convertedChars, p.mess, chat_input.toWideString().c_str(), CHAT_SIZE - 1);
						p.mess[convertedChars - 1] = '\0';

						send_packet(&p);
						chat_input.clear();
						chat_mode = false;
					}
					else {
						chat_mode = true;
					}
				}
				else if (false == chat_mode) {

					if (duration >= 1) {
						int direction = -1;
						switch (event.key.code) {
						case sf::Keyboard::Left:
							direction = 2;
							myPlayer.m_direction = LEFT;
							break;
						case sf::Keyboard::Right:
							direction = 3;
							myPlayer.m_direction = RIGHT;
							break;
						case sf::Keyboard::Up:
							direction = 0;
							myPlayer.m_direction = UP;
							break;
						case sf::Keyboard::Down:
							direction = 1;
							myPlayer.m_direction = DOWN;
							break;
						case sf::Keyboard::Escape:
							window.close();
							break;
						case sf::Keyboard::A:
							if (attack_duration >= 1) {
								CS_ATTACK_PACKET p;
								p.size = sizeof(p);
								p.type = CS_ATTACK;
								send_packet(&p);
								myPlayer.attack();
								last_attack_time = now;
							}
							break;
						}
						if (-1 != direction) {
							CS_MOVE_PACKET p;
							p.size = sizeof(p);
							p.type = CS_MOVE;
							p.direction = direction;
							send_packet(&p);
							last_move_time = now;
						}
					}
					/*int direction = -1;
					switch (event.key.code) {
					case sf::Keyboard::Left:
						direction = 2;
						myPlayer.m_direction = LEFT;
						break;
					case sf::Keyboard::Right:
						direction = 3;
						myPlayer.m_direction = RIGHT;
						break;
					case sf::Keyboard::Up:
						direction = 0;
						myPlayer.m_direction = UP;
						break;
					case sf::Keyboard::Down:
						direction = 1;
						myPlayer.m_direction = DOWN;
						break;
					case sf::Keyboard::Escape:
						window.close();
						break;
					case sf::Keyboard::A:
						if (attack_duration >= 1) {
							CS_ATTACK_PACKET p;
							p.size = sizeof(p);
							p.type = CS_ATTACK;
							send_packet(&p);
							myPlayer.attack();
							last_attack_time = now;
						}
						break;
					}

					if (-1 != direction) {
						CS_MOVE_PACKET p;
						p.size = sizeof(p);
						p.type = CS_MOVE;
						p.direction = direction;
						send_packet(&p);
						last_move_time = now;
					}
				}
			}*/
				}
			}

			if (event.type == sf::Event::TextEntered) {
				if (chat_mode) {
					// 백스페이스
					if (event.text.unicode == 8) {
						if (chat_input.getSize() > 0) {
							chat_input.erase(chat_input.getSize() - 1);
						}
					}
					else {
						// 0 ~ 127 : ASCII 코드만 입력받고, 엔터키는 입력X
						if (event.text.unicode < 128 && event.text.unicode != 13) {
							chat_input += event.text.unicode;
						}
					}

					// esc -> 채팅 모드 종료
					if (event.text.unicode == 27) {
						chat_mode = false;
						chat_input.clear();
					}
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