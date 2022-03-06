#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define LED_NUM 3
#define BUF_LEN 80

#define GAME_LENGTH 12
#define TIME_DELAY 1 // Time Delay in Seconds
#define WAIT_FOR_PLAYER 10

char getch(void);
void flesh_led();

char led_on[LED_NUM][BUF_LEN]  = {"LED1 1", "LED2 1", "LED3 1"};
char led_off[LED_NUM][BUF_LEN] = {"LED1 0", "LED2 0", "LED3 0"};


int file_desc;
int ret_val;
char tmp[BUF_LEN];
char finish;

void _simon_game_(void)
{
    char game_sequence[GAME_LENGTH];

    for (size_t game = 1; game < GAME_LENGTH && !finish; game++)
    {
        // Reset memory
        memset(game_sequence, 0, GAME_LENGTH);

        // Game Sequence
        for (size_t gs = 0; gs < game; gs++)
        {
            game_sequence[gs] = rand() % LED_NUM + 1;
        }
        
        // Openning the driver
        file_desc = open("/dev/gpio_driver", O_RDWR);

        if(file_desc < 0)
        {
            printf("Error, 'gpio_driver' not opened\n");
            continue;
        }

        // LED on/off
        for (size_t i = 0; i < game; i++)
        {
            int gs = game_sequence[i] - 1;
            strcpy(tmp, led_on[gs]);
           
            // write to drivere
            ret_val = write(file_desc, tmp, BUF_LEN);
            sleep(TIME_DELAY);

            strcpy(tmp, led_off[gs]);
            ret_val = write(file_desc, tmp, BUF_LEN);
            sleep(TIME_DELAY);
        }

        // Closing driver
        close(file_desc);

         // Waiting for player to repeat the sequence
        printf("Your move\n");
        sleep(WAIT_FOR_PLAYER);

        // Openning the driver
        file_desc = open("/dev/gpio_driver", O_RDWR);

        if(file_desc < 0)
        {
            printf("Error, 'gpio_driver' not opened\n");
            continue;
        }

        // Reset memory
        memset(tmp, 0, BUF_LEN);

        // Closing driver
        ret_val = read(file_desc, tmp, BUF_LEN);
        close(file_desc);

        if(ret_val < 0)
        {
            printf("Error\n");
            game = 0;
            continue;
        }

        // Converting number to ascii val.
        for (size_t i = 0; i < strlen(game_sequence); i++)
        {
            game_sequence[i] += '0';            
        }

        // Comparing user input and game seq.
        if (strcmp(tmp, game_sequence) != 0)
        {
            printf("\nBetter Luck Next Time :(\n");
            printf("Game seq. : %s\n", game_sequence);
            printf("Your input: %s\n", tmp);
            
            flesh_led();
            
            game = 0;
        }
        else
        {
            printf("\nNext level !!!\n\n");
        }

        if (game == GAME_LENGTH - 1)
        {
            flesh_led();
            flesh_led();
            printf("\nYOU WON\n");
            finish = 1;
        }
               
    }

}

void flesh_led(void)
{
    // Openning the driver
    file_desc = open("/dev/gpio_driver", O_RDWR);

    if(file_desc < 0)
    {
        printf("Error, 'gpio_driver' not opened\n");
        return;
    }

    // LED on/off
    for (size_t i = 0; i < 2; i++)
    {
        for (size_t i = 0; i < LED_NUM; i++)
        {
            strcpy(tmp, led_on[i]);
            ret_val = write(file_desc, tmp, BUF_LEN);
        }

        sleep(TIME_DELAY);  
              
        for (size_t i = 0; i < LED_NUM; i++)
        {
            strcpy(tmp, led_off[i]);
            ret_val = write(file_desc, tmp, BUF_LEN);
        }
        
        sleep(TIME_DELAY); 
    }

    // Closing driver
    close(file_desc);

    // Reset memory
    memset(tmp, 0, BUF_LEN);
}

void* _finish_ (void *pParam)
{
    char c;
    while(1)
    {
        // Getting user input from keybord
        c = getch();

        if (c == 'q' || c == 'Q' || finish)
        {
            // Finish game
            finish = 1;
            
            break;
        }
    }
    return 0;
}

int main()
{
    // Seeding the random number gen.
    srand((unsigned) time(NULL));
    pthread_t pFinish;

    // Creating a thread
    pthread_create(&pFinish, NULL, _finish_, 0);

    printf("##############################\n");
    printf("\tSimon Game\n");
    printf("##############################\n");

    // Fleshing LED for Start
    flesh_led();

    // Staring Simon Game
    _simon_game_();

    pthread_join(pFinish, NULL);

    flesh_led();
    printf("THE END\n");
    printf("gg\n");

    return 0;
}