#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <grp.h>
#include <vector>
#include <string.h>

using namespace std;

#define GPIO_BASE 0x3F200000 // GPIO Base Addr.
#define GPIO_LEN 0xB4 //GPIO memory length -> ?

volatile unsigned* gpio;

void get_user_in_group(const char* username, const char* groupname);
void setup_io();
int read_gpio(int pin);

int main()
{

    //TODO void get_host_name()
    char szHostName[32];
    if (0 == gethostname(szHostName, sizeof(szHostName)))
    {
        cout << "Hello from " << szHostName << endl;
    }
    else
    {
        cout << "Unable to get host name. " << endl;
    }

    get_user_in_group("raspberrypi", "gpio");

    setup_io();

    // TODO Unit test
    *(gpio + 1) = (*(gpio + 1)) & ~(7 << 21) | (1 << 21); // Pin 17 as out
    cout << "Initial status: " << read_gpio(17) << endl;

    *(gpio + 7) = 1 << 17; // Pin 17 as high
    cout << "Status after setting pin as high: " << read_gpio(17) << endl;

    *(gpio + 10) = 1 << 17; // Pin 17 as low
    cout << "Status after setting pin as low: " << read_gpio(17) << endl;

    //gpioInitialise();

    return 0;
}

//TODO Convert to bool
/// <summary>
/// Make sure user is added to appropriate group which has sudo access to /dev/gpiomem
/// </summary>
/// <param name="username">Username</param>
/// <param name="groupname"></param>
void get_user_in_group(const char* username, const char* groupname)
{
    struct group* grp = getgrnam(groupname);
    if (!grp)
    {
        cerr << "Group not found: " << groupname << endl;
    }

    int ngroups = 0;
    getgrouplist(username, getgid(), nullptr, &ngroups);

    vector<gid_t> groups(ngroups);
    getgrouplist(username, getgid(), groups.data(), &ngroups);

    //TODO Add flag -> cout user found in group -> flag = 1 -> cout if not found -> lambda expression
    for (gid_t gid : groups)
    {
        if (gid == grp->gr_gid)
        {
            cout << username << " found in group " << grp->gr_name << endl;
        }
    }
}

/// <summary>
/// 
/// </summary>
void setup_io()
{
    int mem_fd;
    void* gpio_map;

    // Open /dev/gpiomem
    if ((mem_fd = open("/dev/gpiomem", O_RDWR | O_SYNC)) < 0)
    {
        cerr << "Can't open /dev/gpio/mem: " << strerror(errno) << endl;
        exit(-1);
    }

    gpio_map = mmap(
        NULL,// Any address
        GPIO_LEN,
        PROT_READ | PROT_WRITE, // Enable reading and writing
        MAP_SHARED, // Share with other processes
        mem_fd, //File to map
        0 // Offset to GPIO peripheral
    );

    close(mem_fd);

    if (gpio_map == MAP_FAILED)
    {
        cerr << "mmap error: " << gpio_map << endl;
        exit(-1);
    }

    gpio = (volatile unsigned*)gpio_map;
}

/// <summary>
/// 
/// </summary>
/// <param name="pin"></param>
/// <returns></returns>
int read_gpio(int pin)
{
    int reg = pin / 32; // GPIO pins are sets of 32
    int shift = pin % 32; // Bit position of pin register
    return (*(gpio + 13 + reg) & (1 << shift)) != 0;
}