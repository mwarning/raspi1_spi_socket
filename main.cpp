/***********************************************************************
 * mcp3008SpiTest.cpp. Sample program that tests the mcp3008Spi class.
 * an mcp3008Spi class object (a2d) is created. the a2d object is instantiated
 * using the overloaded constructor. which opens the spidev0.0 device with
 * SPI_MODE_0 (MODE 0) (defined in linux/spi/spidev.h), speed = 1MHz &
 * bitsPerWord=8.
 *
 * call the spiWriteRead function on the a2d object 20 times. Each time make sure
 * that conversion is configured for single ended conversion on CH0
 * i.e. transmit ->  byte1 = 0b00000001 (start bit)
 *                   byte2 = 0b1000000  (SGL/DIF = 1, D2=D1=D0=0)
 *                   byte3 = 0b00000000  (Don't care)
 *      receive  ->  byte1 = junk
 *                   byte2 = junk + b8 + b9
 *                   byte3 = b7 - b0
 *
 * after conversion must merge data[1] and data[2] to get final result
 *
 *
 *
 * *********************************************************************/
#include "mcp3008Spi.h"

#include <vector>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h> /* close */
#include <net/if.h>
#include <signal.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <stddef.h>


using namespace std;

/*
float round(float d)
{
  return floor(d + 0.5);
}
*/

class ValueMap
{
  std::vector<unsigned int> values;
  unsigned int max_input;
  unsigned int max_output;

public:

  ValueMap(unsigned int max_input = 1023, unsigned int max_output = 1023)
    : max_input(max_input), max_output(max_output)
  {}

  float map(unsigned int input)
  {
    unsigned int p = values.size();
    if (input > max_input)
    {
      return max_output;
    }

    std::vector<unsigned int>::iterator iter;
    iter = std::lower_bound(values.begin(), values.end(), input);

    if (iter != values.end() && *iter == input)
    {
      p = iter - values.begin();
    }
    else
    {
      p = iter - values.begin() + 1;
      values.insert(iter, input);
    }
    return float(p * max_output) / values.size();
  }

  void print()
  {
    cout << values.size() << " values:" << endl;
    for(unsigned int i = 0; i < values.size(); i++)
    {
      cout << i << " " << values[i];
    }
  }
};

unsigned int readSPI(mcp3008Spi &a2d, int a2dChannel)
{
  unsigned char data[3];
  data[0] = 1;  //  first byte transmitted -> start bit
  data[1] = 0b10000000 |( ((a2dChannel & 7) << 4)); // second byte transmitted -> (SGL/DIF = 1, D2=D1=D0=0)
  data[2] = 0; // third byte transmitted....don't care

  a2d.spiWriteRead(data, sizeof(data) );

  unsigned int a2dVal = 0;
  a2dVal = (data[1]<< 8) & 0b1100000000; //merge data[1] & data[2] to get result
  a2dVal |=  (data[2] & 0xff);
  return a2dVal;
}

class Sender
{
  int m_sock;
  struct sockaddr_in m_sockaddr;

public:

  Sender(std::string address, unsigned int port) :
    m_sock(-1)
  {
    //struct sockaddr_in sockaddr;
    memset(&m_sockaddr, 0, sizeof(sockaddr));
    m_sockaddr.sin_family = AF_INET;
    m_sockaddr.sin_addr.s_addr = inet_addr(address.c_str());
    m_sockaddr.sin_port = htons(port);
  }

  ~Sender() {
    if (m_sock > -1) {
      close(m_sock);
    }
  }

  void init()
  {
    if (m_sock > -1) {
      close(m_sock);
    }

    m_sock = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if (m_sock < 0) {
      printf("Cannot create socket: %s\n", strerror( errno ));
      return; // false;
    }

    const int optval = 1;
    setsockopt( m_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval) );
    //return true;
  }

/*
int send(const char *name, float value)
{
  char buf[1500];
  sprintf(buf, "%s %.2f", name, value);
  return send(&buf[0]);
}
*/

int send(const char *buf)
{
  if (m_sock < 0) {
    init();
    if (m_sock < 0)
      {
      return 0;
    }
  }

  socklen_t addrlen = sizeof(sockaddr);
  int buf_len =  strlen(buf);
  int rc = sendto(m_sock, buf, buf_len, 0, (struct sockaddr*) &m_sockaddr, addrlen);
  if (rc < 0 || rc != buf_len) {
    fprintf( stderr, "%s\n", strerror( errno ) );
    //close(sock);
    return 1;
  }

  return rc;
}
};

class Packet
{
  char m_buf[500];
  size_t m_size;
  public:

  Packet() {
    reset();
  }

  void append(const char *name, float value)
  {
    if (m_size < sizeof(m_buf)) {
      m_size += snprintf(&m_buf[0] + m_size, sizeof(m_buf) - m_size, "%s %.2f\n", name, value);
    }
  }

  int size()
  {
    return m_size;
  }

  char* buf()
  {
    return m_buf;
  }

  void print()
  {
    printf("%s", m_buf);
  }

  void reset()
  {
    m_buf[0] = '\0';
    m_size = 0;
  }
};

static bool running = true;

void sighandler(int sig)
{
  cout<< "Got signal: " << sig << endl;
  running = false;
}

int main(void)
{
  signal(SIGABRT, &sighandler);
  signal(SIGTERM, &sighandler);
  signal(SIGINT, &sighandler);

  Sender sender("192.168.133.1", 8000);
  mcp3008Spi a2d("/dev/spidev0.0", SPI_MODE_0, 1000000, 8);
  /* Input is [0, 999], output is [0, 99] */
  ValueMap values(1000, 100);
  Packet packet;
  int prev_a = 0;
  int a = 0;

  //packet.append("a", 12);
  //packet.append("b", 13);
  //packet.print();
  //return 0;

  std::cout.setf(std::ios_base::fixed, std::ios_base::floatfield);
  std::cout.precision(2);

  while(running)
  {
    packet.reset();

    a = readSPI(a2d, 0);
    float out = values.map(a);

    if (abs(a - prev_a) > 1)
    {
      cout << out << " (" << a << ")" << endl;
      packet.append("a", out);
      prev_a = a;
    }

    if (packet.size())
    {
      sender.send(packet.buf());
    }

    // 50ms
    usleep(1000 * 50);
  }

  return 0;
}
