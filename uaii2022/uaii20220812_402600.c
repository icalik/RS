
/*
    UAII2022 Lindenberg
    2022-08-12 12:58Z
    402600 kHz
    Aerospace Newsky CF-06AH ?
*/

#include <stdio.h>
#include <string.h>
#include <math.h>


typedef unsigned char ui8_t;
typedef short i16_t;
typedef unsigned int ui32_t;


int option_raw = 0,
    option_inv = 0,
    option_b = 0,
    option_timestamp = 0,
    wavloaded = 0;
int wav_channel = 0;     // audio channel: left


#define BAUD_RATE   2400

#define FRAMELEN    128
#define BITFRAMELEN (8*FRAMELEN)

#define HEADLEN 64
#define HEADOFS 0
char header[] = "01010101""01010101""01010101""01010101""01010101"  // preamble
                "10110100""00101011""11000110";
char buf[HEADLEN+1] = "xxxxxxxxxx\0";
int bufpos = 0;

char frame_bits[BITFRAMELEN+1];
ui8_t frame_bytes[FRAMELEN+1];

int sample_rate = 0, bits_sample = 0, channels = 0;
float samples_per_bit = 0;

int read_wav_header(FILE *fp, int *sr, int *bs, int *ch) {
    char txt[5] = "\0\0\0\0\0";
    int byte, num, i;

    if (fseek(fp, 0L, SEEK_SET)) return -1;
    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "RIFF", 4)) return -1;
    if (fseek(fp, 8L, SEEK_SET)) return -1;
    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "WAVE", 4)) return -1;

    if (fseek(fp, 22L, SEEK_SET)) return -1;
    num = 0;
    for (i = 0; i < 2; i++) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;
        num |= (byte << (8*i));
    }
    *ch = num;

    // if (fseek(fp, 24L, SEEK_SET)) return -1;
    num = 0;
    for (i = 0; i < 4; i++) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;
        num |= (byte << (8*i));
    }
    *sr = num;

    if (fseek(fp, 34L, SEEK_SET)) return -1;
    num = 0;
    for (i = 0; i < 2; i++) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;
        num |= (byte << (8*i));
    }
    *bs = num;

    if (fseek(fp, 36L, SEEK_SET)) return -1;
    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "data", 4)) return -1;
    if (fseek(fp, 44L, SEEK_SET)) return -1;

    return 0;
}

#define EOF_INT  0x1000000

unsigned long sample_count = 0;
double bitgrenze = 0;

int read_signed_sample(FILE *fp) {  // int = i32_t
    int byte, i, sample=0, s=0;     // EOF -> 0x1000000

    for (i = 0; i < channels; i++) {
                           // i = 0: links bzw. mono
        byte = fgetc(fp);
        if (byte == EOF) return EOF_INT;
        if (i == wav_channel) sample = byte;

        if (bits_sample == 16) {
            byte = fgetc(fp);
            if (byte == EOF) return EOF_INT;
            if (i == wav_channel) sample +=  byte << 8;
        }

    }

    if (bits_sample ==  8)  s = sample-128;   // 8bit: 00..FF, centerpoint 0x80=128
    if (bits_sample == 16)  s = (short)sample;

    sample_count++;

    return s;
}

int par=1, par_alt=1;

int read_bits_fsk(FILE *fp, int *bit, int *len) {
    static int sample;
    int n;
    float l;

    n = 0;
    do{
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        //sample_count++; // in read_signed_sample()
        par_alt = par;
        par =  (sample >= 0) ? 1 : -1;    // 8bit: 0..127,128..255 (-128..-1,0..127)
        n++;
    } while (par*par_alt > 0);

    l = (float)n / samples_per_bit;

    *len = (int)(l+0.5);

    if (!option_inv) *bit = (1+par_alt)/2;  // oben 1, unten -1
    else             *bit = (1-par_alt)/2;  // sdr#<rev1381?, invers: unten 1, oben -1
// *bit = (1+inv*par_alt)/2; // ausser inv=0

    /* Y-offset ? */

    return 0;
}

int bitstart = 0;
unsigned long scount = 0;
int read_rawbit(FILE *fp, int *bit) {
    int sample;
    int sum;

    sum = 0;

    if (bitstart) {
        scount = 0;    // eigentlich scount = 1
        bitgrenze = 0; //   oder bitgrenze = -1
        bitstart = 0;
    }
    bitgrenze += samples_per_bit;

    do {
        sample = read_signed_sample(fp);
        if (sample == EOF_INT) return EOF;
        //sample_count++; // in read_signed_sample()
        //par =  (sample >= 0) ? 1 : -1;    // 8bit: 0..127,128..255 (-128..-1,0..127)
        sum += sample;
        scount++;
    } while (scount < bitgrenze);  // n < samples_per_bit

    if (sum >= 0) *bit = 1;
    else          *bit = 0;

    if (option_inv) *bit ^= 1;

    return 0;
}

int compare() {
    int i=0;
    while ((i < HEADLEN) && (buf[(bufpos+i) % HEADLEN] == header[HEADLEN+HEADOFS-1-i])) {
        i++;
    }
    return i;
}

char inv(char c) {
    if (c == '0') return '1';
    if (c == '1') return '0';
    return c;
}

int compare2() {
    int i=0;
    while ((i < HEADLEN) && (buf[(bufpos+i) % HEADLEN] == inv(header[HEADLEN+HEADOFS-1-i]))) {
        i++;
    }
    return i;
}


char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};


int print_frame() {
    int i, j;

    for (j = 0; j < FRAMELEN; j++) {
        ui8_t byteval = 0;
        ui8_t d = 1;
        for (i = 0; i < 8; i++) { /* little endian */
            if (frame_bits[8*j+i] & 1) byteval += d;
            d <<= 1;
        }
        frame_bytes[j] = byteval;
    }

    if (option_raw) {
        if (option_raw == 1) {
            for (j = 0; j < FRAMELEN; j++) {
                printf("%02X ", frame_bytes[j]);
            }
        }
        else {
            for (j = 0; j < BITFRAMELEN; j++) {
                printf("%c", frame_bits[j]);
                if (j % 8 == 7) printf(" ");
            }
        }
        printf("\n");
    }
    else {
        ui32_t val;
        int ival;
        ui32_t gpstime;
        ui32_t std, min, ms;
        int wday;
        float sek = 0.0f;

        val = 0;
        for (i = 0; i < 4; i++) val |= frame_bytes[16+i] << (8*i);

        gpstime = val;

        ms = gpstime % 1000;
        gpstime /= 1000;

        wday = (gpstime / (24 * 3600)) % 7;
        if ((wday < 0) || (wday > 6)) wday = 0;

        gpstime %= (24*3600);

        std =  gpstime / 3600;
        min = (gpstime % 3600) / 60;
        sek =  gpstime % 60 + ms/1000.0;

        printf("%s ", weekday[wday]);

        printf("%02d:%02d:%06.3f ", std, min, sek);
        printf(" ");

        val = 0;
        for (i = 0; i < 4; i++) val |= frame_bytes[20+i] << (8*i);
        ival = (int)val;
        float lon = ival / 1e7;
        printf(" lon: %.4f ", lon);

        val = 0;
        for (i = 0; i < 4; i++) val |= frame_bytes[24+i] << (8*i);
        ival = (int)val;
        float lat = ival / 1e7;
        printf(" lat: %.4f ", lat);

        val = 0;
        for (i = 0; i < 4; i++) val |= frame_bytes[28+i] << (8*i);
        ival = (int)val;
        float alt = ival / 1e3;
        printf(" alt: %.1f ", alt);  // MSL


        i16_t val16;
        val16 = 0;
        for (i = 0; i < 2; i++) val16 |= frame_bytes[32+i] << (8*i);
        float vE = val16/100.0f;
        val16 = 0;
        for (i = 0; i < 2; i++) val16 |= frame_bytes[34+i] << (8*i);
        float vN = val16/100.0f;
        val16 = 0;
        for (i = 0; i < 2; i++) val16 |= frame_bytes[36+i] << (8*i);
        float vU = -val16/100.0f;
        //printf(" (%.2f,%.2f,%.2f) ", vE, vN, vU);
        float vH = sqrt(vN*vN+vE*vE);
        float vD = atan2(vE, vN) * 180.0 / M_PI;
        if (vD < 0) vD += 360;
        printf("  vH: %.2f D: %.2f vV: %.2f ", vH, vD, vU);


        printf("\n");
    }

    return 0;
}

int main(int argc, char **argv) {

    FILE *fp;
    char *fpname;

    int i, j, h, bit, len;
    int bit_count, frames;
    int header_found = 0;



    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -i\n");
            fprintf(stderr, "       -b\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;
        }
        else if   (strcmp(*argv, "-b" ) == 0) { option_b = 1; }
        else if   (strcmp(*argv, "-t" ) == 0) { option_timestamp = 1; }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            option_raw = 1;
        }
        else if ( (strcmp(*argv, "-R") == 0) || (strcmp(*argv, "--RAW") == 0) ) {
            option_raw = 2;
        }
        else {
            fp = fopen(*argv, "rb");
            if (fp == NULL) {
                fprintf(stderr, "%s konnte nicht geoeffnet werden\n", *argv);
                return -1;
            }
            wavloaded = 1;
        }
        ++argv;
    }
    if (!wavloaded) fp = stdin;


    i = read_wav_header(fp, &sample_rate, &bits_sample, &channels);
    if (i) return -1;
    fprintf(stderr, "sample_rate: %d\n", sample_rate);
    fprintf(stderr, "bits       : %d\n", bits_sample);
    fprintf(stderr, "channels   : %d\n", channels);

    if ((bits_sample != 8) && (bits_sample != 16)) return -1;

    samples_per_bit = sample_rate/(float)BAUD_RATE;

    fprintf(stderr, "samples/bit: %.2f\n", samples_per_bit);


    bit_count = 0;
    frames = 0;
    while (!read_bits_fsk(fp, &bit, &len)) {

        if (len == 0) {
            bufpos--;
            if (bufpos < 0) bufpos = HEADLEN-1;
            buf[bufpos] = 'x';
            continue;
        }


        for (j = 0; j < len; j++) {
            bufpos--;
            if (bufpos < 0) bufpos = HEADLEN-1;
            buf[bufpos] = 0x30 + bit;

            if (!header_found)
            {
                h = compare(); //h2 = compare2();
                if ((h >= HEADLEN)) {
                    header_found = 1;
                    fflush(stdout);
                    if (option_timestamp) printf("<%8.3f> ", sample_count/(double)sample_rate);
                    strncpy(frame_bits, header, HEADLEN);
                    bit_count += HEADLEN;
                    frames++;
                }
            }
            else
            {
                frame_bits[bit_count] = 0x30 + bit;
                bit_count += 1;
            }

            if (bit_count >= BITFRAMELEN) {
                bit_count = 0;
                header_found = 0;

                print_frame();
            }

        }
        if (header_found && option_b) {
            bitstart = 1;

            while ( bit_count < BITFRAMELEN ) {
                if (read_rawbit(fp, &bit) == EOF) break;
                frame_bits[bit_count] = 0x30 + bit;
                bit_count += 1;
            }

            bit_count = 0;
            header_found = 0;

            print_frame();
        }
    }
    printf("\n");

    fclose(fp);

    return 0;
}

