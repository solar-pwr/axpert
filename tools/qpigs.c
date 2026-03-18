#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>

uint16_t crcTable[16]= {
	0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
	0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef
};

uint16_t calcCrc(uint8_t *data, uint8_t len) {
	uint16_t crc;

	uint8_t tmp;
	uint8_t crcH;
	uint8_t crcL;

	crc = 0;
	while (len--) {
		tmp = ((uint8_t)(crc>>8))>>4;
		crc <<= 4;
		crc ^= crcTable[tmp^(*data>>4)];
		tmp = ((uint8_t)(crc>>8))>>4;
		crc <<= 4;
		crc ^= crcTable[tmp^(*data&0x0f)];
		data++;
	}
	crcL = crc & 0xff;
	crcH = crc >> 8;

	if (crcL == 0x28 || crcL == 0x0d || crcL == 0x0a) crcL++;
	if (crcH == 0x28 || crcH == 0x0d || crcH == 0x0a) crcH++;
	return (uint16_t) crcH << 8 | crcL;
}

int main(int argc, char *argv[]) {
	int serial;
	struct termios tty;
	uint8_t cmd[] = "QPIGS";
	uint16_t crcCalc, crcRecv;
	uint8_t scrc[3];
	uint8_t buf[256];
	int nread, i, loc, locIncr;

	if (argc != 2) {
		printf("Usage: %s /dev/ttyXXXX\n", argv[0]);
		return 1;
	}
	serial = open(argv[1], O_RDWR);
	if (serial == -1) {
		printf("Can't open %s\n", argv[1]);
		close(serial);
		return 1;
	}
	tcgetattr(serial, &tty);
	tty.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
	tty.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
	tty.c_cflag &= ~CSIZE; // Clear all bits that set the data size
	tty.c_cflag |= CS8; // 8 bits per byte (most common)
	tty.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control (most common)
	tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)

	tty.c_lflag &= ~ICANON;
	tty.c_lflag &= ~ECHO; // Disable echo
	tty.c_lflag &= ~ECHOE; // Disable erasure
	tty.c_lflag &= ~ECHONL; // Disable new-line echo
	tty.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
	tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
	tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes

	tty.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
	tty.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed
  // tty.c_oflag &= ~OXTABS; // Prevent conversion of tabs to spaces (NOT PRESENT ON LINUX)
  // tty.c_oflag &= ~ONOEOT; // Prevent removal of C-d chars (0x004) in output (NOT PRESENT ON LINUX)

	tty.c_cc[VTIME] = 10;    // Wait for up to 1s (10 deciseconds), returning as soon as any data is received.
	tty.c_cc[VMIN] = 0;
	cfsetispeed(&tty, B2400);
	cfsetospeed(&tty, B2400);
	tcsetattr(serial, TCSANOW, &tty);
	crcCalc = calcCrc(cmd, strlen(cmd));
	scrc[0] = crcCalc >> 8;
	scrc[1] = crcCalc;
	scrc[2] = 0x0d;
	write(serial, cmd, strlen(cmd));
	write(serial, scrc, 3);
	printf("Sent:\n%s [CRC: %02hhx %02hhx]\n\n", cmd, scrc[0], scrc[1]);
	for (i = 0; i < 256; i += nread) {
		nread = read(serial, buf + i, 1);
		if (nread == 0) break;
		if (buf[i] == 13) {
			i += nread;
			break;
		}
	}
	if (!i) {
		printf("No response\n");
		close(serial);
		return 1;
	}
	if (buf[i - 1] != 13) {
		printf("%i characters read, no EOL, last character %hhx\n", i, buf[i - 1]);
		close(serial);
		return 1;
	}
	buf[i - 1] = 0;
	crcCalc = calcCrc(buf, i - 3);
	crcRecv = ((uint16_t) buf[i - 3]) << 8 | buf[i - 2];
	if (crcCalc != crcRecv) {
		printf("Invalid checksum %hx (read) != %hx (calc)\n---\n", crcRecv, crcCalc);
		printf("%s\n---\n", buf);
		close(serial);
		return 1;
	}
	buf[i - 3] = 0;
	printf("Received:\n");
	printf("%s\n", buf);

	return 0;
}
