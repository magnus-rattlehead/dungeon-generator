#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "dungeon.h"

#define NUM_EDGES 4
#define DRAWING_OUT_FILE "test.txt"

struct Point {
    long long x, y;
};

struct Room {
    struct Point p;
    struct Room *edges[NUM_EDGES];//0 north, 1 east, 2 south, 3 west
};

struct Dungeon {
    unsigned long long num_rooms;
    struct Room **created;
    char **drawing;
    long long min_y, max_y, min_x, max_x;
};

static long long contains_point(long long x, long long y, struct Dungeon *d) {

    for(unsigned long long i = 0; i < d->num_rooms; i++) {
        if(d->created[i]->p.x == x && d->created[i]->p.y == y) {
            return i;//point found
        }
    }

    return -1;//not found
}

static int opposite(int dir) {
    switch(dir) {
        case 0:
            return 2;
        case 1:
            return 3;
        case 2:
            return 0;
        case 3:
            return 1;
        default:
            return -1;
    }
}

static long long my_abs(long long num) {
    if(num < 0) {
        return -1 * num;
    }

    return num;
}

struct Dungeon *create_dungeon(unsigned long long depth) {
    //srand(time(NULL));
    
    
    struct Dungeon *dungeon = (struct Dungeon *)malloc(sizeof(struct Dungeon));
    
    if(!dungeon) {
        printf("Malloc failed!\n");
        exit(1);
    }

    dungeon->created = (struct Room **)malloc((depth) * sizeof(struct Room*));
    if(!dungeon->created) {
        printf("Malloc failed!\n");
        exit(1);
    }

    dungeon->num_rooms = 1;

    
    struct Room *current_room = (struct Room *)malloc(sizeof(struct Room));
    if(!current_room) {
        printf("Malloc failed!\n");
        exit(1);
    }

    for(int i = 0; i < NUM_EDGES; i++) {
        current_room->edges[i] = NULL;
    }
    current_room->p.x = 0;
    current_room->p.y = 0;
    dungeon->created[0] = current_room;
    long long dx = 0, dy = 0, min_y = 0, max_y = 0, min_x = 0, max_x = 0;
    for(unsigned long long i = 1; i < depth; i++) {
        int dir = 0;
        //printf("%d\n", i);
        

        dir = rand() % NUM_EDGES;//works since NUM_EDGES is a power of 2
        bool checked[NUM_EDGES] = {false, false, false, false};
        while(!(checked[0] && checked[1] && checked[2] && checked[3])) {
            if(current_room->edges[dir]) {
                //printf("Generating random dir again\n");
                checked[dir] = true;
                dir = rand() % NUM_EDGES;
            } else {
                //printf("unique dir\n");
                break;
            }
        }
        //printf("dir: %d, dx: %d, dy: %d\n", dir, dx, dy);
        switch(dir) {
            case 0:
                dy++;
                break;
            case 1:
                dx++;
                break;
            case 2:
                dy--;
                break;
            case 3:
                dx--;
            break;
        }

        if(dy < min_y) {
            min_y = dy;
        } else if (dy > max_y) {
            max_y = dy;
        }

        if(dx < min_x) {
            min_x = dx;
        } else if(dx > max_x) {
            max_x = dx;
        }

        //printf("checking if room at %d, %d exists\n", dx, dy);
        long long check = contains_point(dx, dy, dungeon);
        if(check == -1) {
            //printf("creating new room #%d with x and y: %d %d\n", i, dx, dy);
            current_room->edges[dir] = (struct Room *)malloc(sizeof(struct Room));
            if(!current_room->edges[dir]) {
                printf("Malloc failed!\n");
                exit(1);
            }
            current_room->edges[dir]->p.x = dx;
            current_room->edges[dir]->p.y = dy;
            
            dungeon->created[dungeon->num_rooms] = current_room->edges[dir];
            //printf("%d\n", dungeon->num_rooms);
            dungeon->num_rooms++;
            for(int j = 0; j < NUM_EDGES; j++) {
                current_room->edges[dir]->edges[j] = NULL;
            }
        } else {

            printf("Room already exists @ %lld %lld!\n", dx, dy);
            printf("%lld %lld %d\n", current_room->p.x, current_room->p.y, dir);
            current_room->edges[dir] = dungeon->created[check];
            printf("%p, %lld, %lld\n", dungeon->created[check], dungeon->created[check]->p.x, dungeon->created[check]->p.y);
            dungeon->created[dungeon->num_rooms] = NULL;
            i--;
        }
        
        int opposite_dir = opposite(dir);
        //printf("connecting next room and current room, %d to %d\n", dir, opposite_dir);
        current_room->edges[dir]->edges[opposite_dir] = current_room;

        current_room = current_room->edges[dir];
        //printf("setting %p's x and y to %d %d\n", current_room, dx, dy);
        
    }

    //printf("%d\n", dungeon->num_rooms);
    dungeon->created = (struct Room **)realloc(dungeon->created, (dungeon->num_rooms)* sizeof(struct Room *));
    dungeon->drawing = (char **)malloc(((my_abs(min_y) + my_abs(max_y))*2 + 1) * sizeof(char *));
    for(long long j = 0; j <= (my_abs(min_y) + my_abs(max_y))*2; j++) {
        dungeon->drawing[j] = (char *)malloc((my_abs(min_x) + my_abs(max_x))*2 + 1);
    }
    dungeon->min_x = min_x;
    dungeon->min_y = min_y;
    dungeon->max_x = max_x;
    dungeon->max_y = max_y;
    for(long long y = 0; y <= (my_abs(min_y) + my_abs(max_y))*2; y++) {
        for(long long x = 0; x <= (my_abs(min_x) + my_abs(max_x))*2; x++) {
            dungeon->drawing[y][x] = ' ';
        }
    }

    //long long x_offset = 0, y_offset = 0;
    for(unsigned long long j = 0; j < dungeon->num_rooms; j++) {
        if(dungeon->created[j] != NULL) {
            //printf("Attempting to access %lld %lld\n", dungeon->created[i]->p.y + my_abs(min_y) + y_offset, dungeon->created[i]->p.x + my_abs(min_x) + x_offset);
            dungeon->drawing[(dungeon->created[j]->p.y + my_abs(min_y))*2][(dungeon->created[j]->p.x + my_abs(min_x))*2] = '#';
            if(dungeon->created[j]->edges[0] != NULL) {
                dungeon->drawing[(dungeon->created[j]->p.y + my_abs(min_y))*2 + 1][(dungeon->created[j]->p.x + my_abs(min_x))*2] = '|';
            }
            if(dungeon->created[j]->edges[1] != NULL) {
                dungeon->drawing[(dungeon->created[j]->p.y + my_abs(min_y))*2][(dungeon->created[j]->p.x + my_abs(min_x))*2 + 1] = '-';
            }
        }
        

    }

    return dungeon;
}

void print_dungeon(struct Dungeon *d) {
    for(unsigned long long i = 0; i < d->num_rooms; i++) {
        if(d->created[i] != NULL) {
            printf("Room %p, x: %lld, y: %lld, edges: ", d->created[i], d->created[i]->p.x, d->created[i]->p.y);
            for(int j = 0; j < NUM_EDGES; j++) {
                if(d->created[i]->edges[j] != NULL) {
                 printf("%d %p ", j, d->created[i]->edges[j]);
                }
            
            }
            printf("\n");
        }
        
    }
}

void print_drawing(struct Dungeon *d) {
    FILE *file = fopen(DRAWING_OUT_FILE, "w");
    if(file == NULL) {
        printf("gg\n");
        exit(-1);
    }
    for(long long y = (my_abs(d->min_y) + my_abs(d->max_y))*2; y >=0; y--) {
        for(long long x = 0; x <= (my_abs(d->min_x) + my_abs(d->max_x))*2; x++) {
            fprintf(file, "%c", d->drawing[y][x]);
        }
        fprintf(file, "\n");
    }
    fclose(file);
}

void destroy_dungeon(struct Dungeon *d) {
    unsigned long long freed = 0;
    for(unsigned long long i = 0; i < d->num_rooms; i++) {
        if(d->created[i] != NULL) {
            //printf("Freeing %p, item %d\n", d->created[i], i);
            free(d->created[i]);
            freed++;
            //printf("Item %d successfully freed\n", i);
        }
        
    }
    printf("Freed: %lld objects\n", freed);
    free(d->created);
    for(long long i = 0; i <= (my_abs(d->min_y) + my_abs(d->max_y))*2; i++) {
        free(d->drawing[i]);
    }
    free(d->drawing);
    free(d);
}