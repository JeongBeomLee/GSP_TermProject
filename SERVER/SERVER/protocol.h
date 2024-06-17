constexpr int PORT_NUM  = 4000;
constexpr int NAME_SIZE = 20;
constexpr int CHAT_SIZE = 100;
constexpr int BUF_SIZE  = 200;

constexpr int NUM_OBSTACLES = 50000;
constexpr int MAX_USER = 10000;
constexpr int MAX_NPC  = 200000;

constexpr int W_WIDTH  = 2000;
constexpr int W_HEIGHT = 2000;

constexpr int VIEW_RANGE = 7;

// sectoring
constexpr int SECTOR_WIDTH	= 25;
constexpr int SECTOR_HEIGHT = 25;
constexpr int SECTOR_COLS	= W_WIDTH / SECTOR_WIDTH;
constexpr int SECTOR_ROWS	= W_HEIGHT / SECTOR_HEIGHT;

// Packet ID
constexpr char CS_LOGIN		= 0;
constexpr char CS_MOVE		= 1;
constexpr char CS_CHAT		= 2;
constexpr char CS_ATTACK	= 3;	// 4 방향 공격
constexpr char CS_TELEPORT	= 4;	// RANDOM한 위치로 Teleport, Stress Test할 때 Hot Spot현상을 피하기 위해 구현
constexpr char CS_LOGOUT	= 5;	// 클라이언트에서 정상적으로 접속을 종료하는 패킷

constexpr char SC_LOGIN_INFO	= 2;
constexpr char SC_LOGIN_FAIL	= 3;
constexpr char SC_LOGIN_OK		= 4;
constexpr char SC_ADD_OBJECT	= 5;
constexpr char SC_REMOVE_OBJECT = 6;
constexpr char SC_MOVE_OBJECT	= 7;
constexpr char SC_CHAT			= 8;
constexpr char SC_STAT_CHANGE	= 9;
constexpr char SC_PLAYER_ATTACK	= 10;

enum Visual { 
	LEA,		// 플레이어
	BALOONEER,	// 몬스터 1 (Peace, 고정)
	PINCERON,	// 몬스터 2 (Peace, 랜덤움직임)
	FROBBIT,	// 몬스터 3 (Aggro, 고정)
	HEDGEHAG,	// 몬스터 4 (Aggro, 랜덤움직임)
};

#pragma pack (push, 1)
struct CS_LOGIN_PACKET {
	unsigned short size;
	char		   type;
	char		   name[NAME_SIZE];
};

struct CS_MOVE_PACKET {
	unsigned short size;
	char		   type;
	char		   direction;  // 0 : UP, 1 : DOWN, 2 : LEFT, 3 : RIGHT
	unsigned       move_time;
};

struct CS_CHAT_PACKET {
	unsigned short size;	// 크기가 가변이다, mess가 작으면 size도 줄이자.
	char		   type;
	char		   mess[CHAT_SIZE];
};

struct CS_ATTACK_PACKET {
	unsigned short size;
	char		   type;
}; 

struct CS_TELEPORT_PACKET {
	unsigned short size;
	char		   type;
};

struct CS_LOGOUT_PACKET {
	unsigned short size;
	char		   type;
};

struct SC_LOGIN_INFO_PACKET {
	unsigned short size;
	char		   type;
	int			   visual;
	int			   id;
	int			   hp;
	int			   max_hp;
	int			   exp;
	int			   level;
	short		   x, y;
};

struct SC_ADD_OBJECT_PACKET {
	unsigned short size;
	char		   type;
	int			   id;
	int			   visual;
	short		   x, y;
	char		   name[NAME_SIZE];
};

struct SC_REMOVE_OBJECT_PACKET {
	unsigned short size;
	char		   type;
	int			   id;
};

struct SC_MOVE_OBJECT_PACKET {
	unsigned short size;
	char		   type;
	int			   id;
	short		   x,y;
	unsigned int   move_time;
};

struct SC_CHAT_PACKET {
	unsigned short size;
	char		   type;
	int			   id;
	char		   mess[CHAT_SIZE];
};

struct SC_LOGIN_FAIL_PACKET {
	unsigned short size;
	char		   type;
};

struct SC_STAT_CHANGE_PACKET {
	unsigned short size;
	char		   type;
	int			   hp;
	int			   max_hp;
	int			   exp;
	int			   level;
};

struct SC_LOGIN_OK_PACKET {
	unsigned short size;
	char		   type;
};

struct SC_PLAYER_ATTACK_PACKET {
	unsigned short size;
	char		   type;
	int			   id;
};
#pragma pack (pop)