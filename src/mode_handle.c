#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include "game_logic.h"
#include "login.h"
#include "map.h"
#include "player.h"
#include "chest.h"
#include "monster.h"
#include "custom_effects.h"
#include "mode_handle.h"
#include "client.h"
#include "events_handler.h"

volatile sig_atomic_t hard_exit_flag = FALSE;
volatile int save_flag = FALSE;
volatile int lock_flag = FALSE;
int set_counter = 1;

void hard_exit_handler(int sig)
{
    hard_exit_flag = 1;
}

/**
 * Main game function
 * its responsible for running the game for single player mode
 */
void init_game_single(account_t *account, int mode)
{
    char key_press = ' ';
    char key[2];
    map_t map;
    player_t player;
    monster_t *mons_arr;
    chest_t *chest_arr;
    int health_holder = 0;
    int boss_arr[TOTAL_LVLS][2];

    /**
     * To pass the values from the loaded save file to current game
     */

    int mons_buffer[MAX_MONSTERS][MONS_ELMNTS];
    int chest_buffer[MAX_CHESTS];

    /**
     * Parse boss monsters
     */
    monster_boss_parser(boss_arr);
    init_player(&player, account->id, SINGLE_MODE);
    /**
     *  When a save game is found load the values from that file and start
     *  the game. Else create a file for that player and start a game from level 0
     *  
     */

    if (access(account->save_file, F_OK) != -1)
    {
        /**
         * - Set buffer arrays to 0
         * - Get the values and load them to memory
         */
        memset_arrays(mons_buffer, chest_buffer);
        if (!load_game(account, &map, &player, mons_buffer, chest_buffer))
        {
            redprint("An error occured while loading your save. Exiting!");
            exit(EXIT_FAILURE);
        }
        else
        {
            /**
             * In @load_game(); map and player got their values
             * to load the values of the alive monsters and unoppened chests the
             * function @set_objects(); set objects is called
             */
            mons_arr = (monster_t *)calloc(sizeof(monster_t), map.level + 3);
            chest_arr = (chest_t *)calloc(sizeof(chest_t), map.level);
            load_map(&map, mons_arr, chest_arr, boss_arr);
            pass_object_values(mons_arr, chest_arr, mons_buffer, chest_buffer, &map);
            update_objects(&map, mons_arr, chest_arr);
        }

        map_set(&map, player.psymbol, player.y, player.x);
    }
    else
    {
        /**
         *  When there's not a save game set level to 1 and start a new game
         */
        map.level = 1;
        mons_arr = (monster_t *)calloc(sizeof(monster_t), map.level + 3);
        chest_arr = (chest_t *)calloc(sizeof(chest_t), map.level);
        load_map(&map, mons_arr, chest_arr, boss_arr);
        add_stats(&player);
        map_set(&map, player.psymbol, player.y, player.x);
    }
    /**
     * Fetch health to the holder var
     */
    health_holder = player.health;
    /**
     * Level change loop
     */
    if (map.level == 1)
        save_game(&map, account, &player, mons_arr, chest_arr);

    while (1)
    {
        /**
         * Main game loop
         */
        while (1)
        {

            /**
            * Movement keys
            */
            key_press = key_input(key);
            if (key_press == LEFT_C ||
                key_press == LEFT_S ||
                key_press == RIGHT_C ||
                key_press == RIGHT_S ||
                key_press == UP_C ||
                key_press == UP_S ||
                key_press == DOWN_C ||
                key_press == DOWN_S)
            {
                player.prev_direction = player.direction;
                player.direction = key_press;
                object_found(&map, &player, key_press, mons_arr, chest_arr);
                move(&map, &player);
            }
            /**
            * To save the game press #
            */
            else if (key_press == '#')
            {
                if (!save_game(&map, account, &player, mons_arr, chest_arr))
                {
                    redprint("CRASH ERROR!");
                    exit(EXIT_FAILURE);
                }
                else
                {
                    system("clear");
                    redprint_slow("Your game has been saved successfully!\nC0ntr0l 1s 4n 1llus10n!\n");
                    exit(EXIT_SUCCESS);
                }
            }
            /**
             * Check if the conditions match to level up
             */
            if (!check_level_up(mons_arr, &map))
            {
                system("clear");
                kill_all(mons_arr, &map);
                level_up(&player, mons_arr, &map);
                break;
            }

            if (!check_game_over_single(&player))

            {
                //system("clear");
                player.health = health_holder;
                player.isDead = FALSE;
                break;
            }

            /**
            * When no direction key is pressed
            */

            system("clear");
            player_check_max_stats(&player);
            to_print(&map, &player, mons_arr, chest_arr);
            update_objects(&map, mons_arr, chest_arr);
            usleep(100000);
            fflush(stderr);
            fflush(stdin);
            fflush(stdout);
        }
        /**
         * In case were break is called the 2 arrays are getting freed and reallocated 
         * next level.
         * Also the map gets loaded and the point values are getting passed to the 2 arrays
         * 
         */
        free(mons_arr);
        mons_arr = NULL;
        free(chest_arr);
        chest_arr = NULL;
        mons_arr = (monster_t *)calloc(sizeof(monster_t), map.level + 3);
        chest_arr = (chest_t *)calloc(sizeof(chest_t), map.level);
        load_map(&map, mons_arr, chest_arr, boss_arr);
        update_objects(&map, mons_arr, chest_arr);
        player.x = 18;
        player.y = 48;
        save_game(&map, account, &player, mons_arr, chest_arr);
        // this is the autosave feature for every level
    }
}

void init_game_multi(account_t *account, client_t *client)
{
    char *net_buffer;
    char *token;

    char comp_load[5];
    char comp_new[5];
    char net_pass_buffer[SOCK_BUFF_SZ];
    char net_pass_buffer_cp[SOCK_BUFF_SZ];
    game_t game;
    int i;

    /***
     * To load from file
     * 
     */
    int monsters[MAX_MONSTERS][MONS_ELMNTS];
    int chests[MAX_CHESTS];

    game.client = client;
    /**
     * Parse boss monsters
     */
    monster_boss_parser(game.boss_arr);

    /**
     * 
     * receive global game id and initialize the player
     * 
     */
    recv(client->sockfd, net_pass_buffer, SOCK_BUFF_SZ, 0);
    token = strtok(net_pass_buffer, NET_DELIM);

    game.client->uid = atoi(token);
    token = strtok(NULL, NET_DELIM);
    strcpy(game.save_game_code, token);

    for (int i = 0; i < 3; i++)
    {
        init_player(&game.players[i], i, MULTI_MODE);
    }
    net_buffer = on_player_update_stats(&game.players[game.client->uid], &game.map);
    send(client->sockfd, net_buffer, SOCK_BUFF_SZ, 0);

    /***
     * Check the hash of the file to be read
     */

    bzero(net_pass_buffer, SOCK_BUFF_SZ);
    recv(client->sockfd, net_pass_buffer, SOCK_BUFF_SZ, 0);
    strcpy(net_pass_buffer_cp, net_pass_buffer);
    token = strtok(net_pass_buffer_cp, NET_DELIM);

    itoa(LOAD_GAME_ID, comp_load, 10);
    itoa(MAP_OPEN_ID, comp_new, 10);

    if (!strcmp(token, comp_new))
    {
        if (!decode_on_map_receive(&game.map, net_pass_buffer))
        {
            redprint("[ERROR] The map file may be corrupted or modified!");
            exit(EXIT_FAILURE);
        }
        add_stats(&game.players[game.client->uid]);
        game.mons_arr = (monster_t *)calloc(sizeof(monster_t), game.map.level + 3);
        game.chest_arr = (chest_t *)calloc(sizeof(chest_t), game.map.level);
        load_map(&game.map, game.mons_arr, game.chest_arr, game.boss_arr);

        /**
     * Fetch health to health holder
     */
        for (i = 0; i < 3; i++)
        {
            game.health_holder[i] = game.players[game.client->uid].health;
        }
    }
    else if (!strcmp(token, comp_load))
    {
        memset_arrays(monsters, chests);
        if (!on_load_game_multi(game.save_game_code, net_pass_buffer, &game.map, game.players, monsters, chests))
        {
            redprint("[ERROR] The save file is either corrupted or modified\n");
            exit(EXIT_FAILURE);
        }
        redprint_slow("\n\n[+] Please wait while the game is loading...");
        game.mons_arr = (monster_t *)calloc(sizeof(monster_t), game.map.level + 3);
        game.chest_arr = (chest_t *)calloc(sizeof(chest_t), game.map.level);
        load_map(&game.map, game.mons_arr, game.chest_arr, game.boss_arr);
        pass_object_values_multi(game.mons_arr, game.chest_arr, monsters, chests, &game.map);
        update_objects(&game.map, game.mons_arr, game.chest_arr);

        for (i = 0; i < 3; i++)
        {
            map_set(&game.map, game.players[i].psymbol, game.players[i].y, game.players[i].x);
        }
    }

    signal(SIGINT, hard_exit_handler);
    /**
     * 
     * Need to fill players array
     */

    pthread_t game_thread;
    pthread_t recv_thread;

    if (pthread_create(&game_thread, NULL, multi_game_handler, (void *)&game) != 0)
    {
        perror("Create game thread");
        //  HARD INTERRUPT SIGNAL
    }

    if (pthread_create(&recv_thread, NULL, multi_recv_handler, (void *)&game) != 0)
    {
        perror("Create receive thread");
    }

    /**
     * 
     * may need to use here pthread_cancel - pthread_cleanup_pop/push
     */

    while (1)
    {
        if (hard_exit_flag == TRUE || save_flag == TRUE)
        {
            sleep(5);
            break;
        }

        /**
         * Reduce CPU consumption
         */

        sleep(3);
    }

    close(client->sockfd);
}

void *multi_game_handler(void *args)
{
    int i;
    char key_press = ' ';
    char key[2];
    game_t *game = (game_t *)args;
    char *net_buffer;

    while (1)
    {
        while (1)
        {

            key_press = key_input(key);
            if ((key_press == LEFT_C ||
                 key_press == LEFT_S ||
                 key_press == RIGHT_C ||
                 key_press == RIGHT_S ||
                 key_press == UP_C ||
                 key_press == UP_S ||
                 key_press == DOWN_C ||
                 key_press == DOWN_S) &&
                game->players[game->client->uid].isDead == FALSE && lock_flag == FALSE)
            {
                game->players[game->client->uid].prev_direction = game->players[game->client->uid].direction;
                game->players[game->client->uid].direction = key_press;
                object_found_multi(game->client, &game->map, &game->players[game->client->uid], key_press, game->mons_arr, game->chest_arr);
                move_multi(&game->map, game->players, game->client->uid);
                net_buffer = on_player_update_stats(&game->players[game->client->uid], &game->map);
                send(game->client->sockfd, net_buffer, SOCK_BUFF_SZ, 0);
            }
            if (key_press == 'n')
            {
                kill_all(game->mons_arr, &game->map);
                for (i = 0; i < game->map.monsters_num; i++)
                {
                    net_buffer = on_monster_death(&game->mons_arr[i]);

                    while (!strcmp(net_buffer, "11"))
                    {
                        net_buffer = NULL;
                        net_buffer = on_monster_death(&game->mons_arr[i]);
                    }

                    send(game->client->sockfd, net_buffer, SOCK_BUFF_SZ, 0);
                    usleep(500000);
                }
            }
            /**
            * Save the game press #
            */

            if ((key_press == '#' && lock_flag == FALSE) || (save_flag == TRUE && lock_flag == FALSE))
            {
                save_game_multi(game->save_game_code, &game->map, game->players, game->mons_arr, game->chest_arr);
                net_buffer = on_player_request_save(game->save_game_code);
                send(game->client->sockfd, net_buffer, SOCK_BUFF_SZ, 0);
                system("clear");
                redprint_slow("Your game has been saved successfully!\nC0ntr0l 1s 4n 1llus10n!\n");
                break;
            }
            /**
            * Check if conditions match to level up
            */
            if (!check_level_up(game->mons_arr, &game->map) && lock_flag == FALSE)
            {
                //bzero(net_buffer, SOCK_BUFF_SZ);
                net_buffer = NULL;
                system("clear");
                kill_all(game->mons_arr, &game->map);
                level_up(&game->players[game->client->uid], game->mons_arr, &game->map);
                break;
            }
            /**
            * Check if conditions match to game over
            */

            if (!check_game_over_multi(game->players, game) && lock_flag == FALSE)
            {
                //bzero(net_buffer, SOCK_BUFF_SZ);
                net_buffer = NULL;
                for (int i = 0; i < 3; i++)
                {
                    game->players[i].health = game->health_holder[i];
                    game->players[i].isDead = FALSE;
                    game->players[i].x = 18 + i;
                    game->players[i].y = 48;
                }
                //break;
            }

            if (hard_exit_flag == TRUE && lock_flag == FALSE)
            {

                net_buffer = on_player_hard_exit();
                send(game->client->sockfd, net_buffer, SOCK_BUFF_SZ, 0);
                system("clear");
                redprint_slow("Your game stopped violently\nC0ntr0l 1s 4n 1llus10n!\n");
                break;
            }
            /**
            * When no direction key is pressed
            */

            on_death_hp_set(&game->map, game->players);
            system("clear");
            player_check_max_stats(&game->players[game->client->uid]);
            update_objects(&game->map, game->mons_arr, game->chest_arr);
            to_print_multi(&game->map, game->players, game->mons_arr, game->chest_arr, game->client->uid);

            usleep(500000);
            fflush(stderr);
            fflush(stdin);
            fflush(stdout);
        }
        /**
         * In case were break is called the 2 arrays are getting freed and reallocated 
         * next level.
         * Also the map gets loaded and the point values are getting passed to the 2 arrays
         * 
         */
        if (save_flag == TRUE || hard_exit_flag == TRUE)
        {
            break;
        }
        net_buffer = NULL;
        free(game->mons_arr);
        game->mons_arr = NULL;
        free(game->chest_arr);
        game->chest_arr = NULL;
        game->mons_arr = (monster_t *)calloc(sizeof(monster_t), game->map.level + 3);
        game->chest_arr = (chest_t *)calloc(sizeof(chest_t), game->map.level);
        load_map(&game->map, game->mons_arr, game->chest_arr, game->boss_arr);
        update_objects(&game->map, game->mons_arr, game->chest_arr);
        for (i = 0; i < 3; i++)
        {
            game->players[i].x = 18 + i;
            game->players[i].y = 48;
        }

        net_buffer = NULL;
    }

    return NULL;
}

void *multi_recv_handler(void *args)
{

    char *response_id;
    char comp[10];
    int receive_sz = 0;
    game_t *game = (game_t *)args;
    char net_buffer[SOCK_BUFF_SZ];
    char *net_buffer_cp;
    while (1)
    {
        receive_sz = recv(game->client->sockfd, net_buffer, SOCK_BUFF_SZ, 0);

        if (receive_sz > 0)
        {
            lock_flag = TRUE;
            net_buffer_cp = (char *)calloc(sizeof(char), strlen(net_buffer));
            strcpy(net_buffer_cp, net_buffer);

            response_id = strtok(net_buffer_cp, NET_DELIM);
            /**
              * Player Functions1
              */
            itoa(PLR_DEATH_ID_P, comp, 10);
            if (!strcmp(response_id, comp))
            {
                if (decode_on_player_death(&game->map, game->players, net_buffer) != 0)
                {
                    hard_exit_flag = TRUE;
                }
                lock_flag = FALSE;
            }

            itoa(PLR_MOVE_ID_P, comp, 10);
            if (!strcmp(response_id, comp))
            {
                if (decode_on_player_move(game->players, net_buffer) != 0)
                {
                    hard_exit_flag = TRUE;
                }
                lock_flag = FALSE;
            }

            itoa(PLR_UPDATE_ID_P, comp, 10);
            if (!strcmp(response_id, comp))
            {
                if (decode_on_player_update_stats(game->players, net_buffer, &game->map) != 0)
                {
                    hard_exit_flag = TRUE;
                }
                lock_flag = FALSE;
            }

            /**
              * Monster Functions
              */

            itoa(MNSTR_DEATH_ID_M, comp, 10);
            if (!strcmp(response_id, comp))
            {
                if (decode_on_monster_death(game->mons_arr, net_buffer, &game->map) != 0)
                {
                    hard_exit_flag = TRUE;
                }
                lock_flag = FALSE;
            }

            itoa(MNSTR_UPDATE_ID_M, comp, 10);
            if (!strcmp(response_id, comp))
            {
                if (decode_on_monster_update_stats(game->mons_arr, net_buffer, &game->map) != 0)
                {
                    hard_exit_flag = TRUE;
                }
                lock_flag = FALSE;
            }

            /**
              * Chest functions
              */
            itoa(CHEST_OPEN_ID_C, comp, 10);
            if (!strcmp(response_id, comp))
            {
                if (decode_on_chest_open(game->chest_arr, net_buffer, &game->map) != 0)
                {
                    hard_exit_flag = TRUE;
                }
                lock_flag = FALSE;
            }

            itoa(SAVE_GAME_ID, comp, 10);
            if (!strcmp(response_id, comp))
            {
                save_game_multi(game->save_game_code, &game->map, game->players, game->mons_arr, game->chest_arr);
                save_flag = TRUE;
                lock_flag = FALSE;
                break;
            }

            itoa(HARD_EXIT_ID, comp, 10);
            if (!strcmp(response_id, comp))
            {
                hard_exit_flag = TRUE;
                lock_flag = FALSE;
                break;
            }
            memset(comp, 0, sizeof(comp));
            free(net_buffer_cp);
            net_buffer_cp = NULL;
        }
        memset(net_buffer, 0, sizeof(net_buffer));
    }

    return NULL;
}
