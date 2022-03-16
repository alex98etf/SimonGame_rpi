# SimonGame_rpi
Simple Simon Game realization with 3 LED and 3 switchs. It consists of a gpio driver and a user application.  
Game length is 12 rounds, and player response is expected within 10 seconds.
# Compiling
Just run thr two make files in each directories.
# Run
#### GPIO driver
Run the command ***insmod gpio_driver.ko*** to install the driver.   
Rum the command ***dmesg*** to see what our major number is.  
Then run the command ***mknod /dev/gpio_driver c <major_number> 0*** to mount our driver.

#### User App
Just run ***./bin/Release/simon_game***

# Removal
Press **q** or **Q** quit the game.  
To remove driver run ***rm /dev/gpio_driver***   
For removing module from the kernel, run ***rmmod gpio_driver***

