struct Point;

struct Room;

struct Dungeon;

struct Dungeon *create_dungeon(unsigned long long depth);

void print_dungeon(struct Dungeon *d);

void print_drawing(struct Dungeon *d);

void destroy_dungeon(struct Dungeon *d);