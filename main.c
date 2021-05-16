#include "dungeon.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {

    if(argc > 2) {
        printf("Too many arguments!\n");
        exit(1);
    }

    struct Dungeon *d = create_dungeon(atoll(argv[1]));
    print_dungeon(d);
    print_drawing(d);
    destroy_dungeon(d);
    return 0;
}