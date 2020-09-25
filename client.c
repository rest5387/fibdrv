#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "bignum.h"

#define FIB_DEV "/dev/fibonacci"

#define LEN_BN_STR 50

static void str_reverse(char *str)
{
    if (!str)
        return;

    int len = strlen(str);

    for (int i = 0; i < len / 2; i++) {
        char tmp = str[i];
        str[i] = str[len - 1 - i];
        str[len - 1 - i] = tmp;
    }
}

static int str_add(char *str_sum, char *str_addend, unsigned int digits)
{
    int len;
    int idx = 0, carry = 0;
    char str_tmp[LEN_BN_STR] = "";

    if (!str_sum || !str_addend || digits == 0)
        return -1;

    str_reverse(str_sum);
    str_reverse(str_addend);

    len = strlen(str_sum) > strlen(str_addend) ? strlen(str_sum)
                                               : strlen(str_addend);
    for (int i = 0, j = 0; (i < len) && (j < len);) {
        int sum = 0;

        if (str_sum[i] != '\0')
            sum += str_sum[i++] - '0';
        if (str_addend[j] != '\0')
            sum += str_addend[j++] - '0';

        if (idx < digits)
            str_tmp[idx++] = (sum + carry) % 10 + '0';

        carry = (sum + carry) / 10;
    }

    if ((idx < digits) && carry)
        str_tmp[idx++] = carry + '0';
    str_tmp[idx] = '\0';

    str_reverse(str_sum);
    str_reverse(str_addend);
    str_reverse(str_tmp);

    snprintf(str_sum, digits, "%s", str_tmp);

    return (int) strlen(str_sum);
}

static int str_multiply(char *multiplicand,
                        char *multiplier,
                        unsigned int digits)
{
    char str_tmp[LEN_BN_STR] = "";
    char str_product[LEN_BN_STR] = "";
    int carry = 0;

    if (!multiplicand || !multiplier || digits == 0 ||
        strlen(multiplier) == 0 || strlen(multiplicand) == 0)
        return -1;

    if ((strlen(multiplicand) == 1 && multiplicand[0] == '0') ||
        (strlen(multiplier) == 1 && multiplier[0] == '0')) {
        snprintf(multiplicand, digits, "%s", "0");
        return (int) strlen(multiplicand);
    }

    str_reverse(multiplicand);
    str_reverse(multiplier);

    for (int i = 0; i < strlen(multiplier); i++) {
        int idx = 0;

        for (int decuple = i; (decuple > 0) && (idx < digits); decuple--)
            str_tmp[idx++] = '0';

        for (int j = 0; j < strlen(multiplicand); j++) {
            int product =
                (multiplier[i] - '0') * (multiplicand[j] - '0') + carry;
            if (idx < digits)
                str_tmp[idx++] = product % 10 + '0';
            carry = product / 10;
        }

        if (idx < digits && carry) {
            str_tmp[idx++] = carry + '0';
            carry = 0;
        }

        str_tmp[idx] = '\0';

        str_reverse(str_tmp);
        str_add(str_product, str_tmp, digits);
    }

    str_reverse(multiplicand);
    str_reverse(multiplier);

    snprintf(multiplicand, digits, "%s", str_product);

    return (int) strlen(multiplicand);
}

static void toString_bigN(bigN fib_seq, char *out_buf, unsigned int digits)
{
    char bn_upper[LEN_BN_STR] = "";
    char bn_lower[LEN_BN_STR] = "";
    char bn_scale[LEN_BN_STR] = "18446744073709551616";

    snprintf(out_buf, digits, "%s", "0");
    snprintf(bn_upper, digits, "%llu", fib_seq.upper);
    snprintf(bn_lower, digits, "%llu", fib_seq.lower);

    str_multiply(bn_upper, bn_scale, digits);
    str_add(out_buf, bn_upper, digits);
    str_add(out_buf, bn_lower, digits);
}



int main()
{
    long long sz;

    bigN bn_buf;
    // char buf[1];
    char fib_str_buf[LEN_BN_STR] = "";
    char write_buf[] = "testing writing";
    int offset = 100; /* TODO: try test something bigger than the limit */

    int fd = open(FIB_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }

    for (int i = 0; i <= offset; i++) {
        sz = write(fd, write_buf, strlen(write_buf));
        printf("Writing to " FIB_DEV ", returned the sequence %lld\n", sz);
    }

    for (int i = 0; i <= offset; i++) {
        lseek(fd, i, SEEK_SET);
        sz = read(fd, &bn_buf, sizeof(bn_buf));
        toString_bigN(bn_buf, fib_str_buf, sizeof(fib_str_buf));
        printf("Reading from " FIB_DEV
               " at offset %d, returned the sequence "
               "%s.\n",
               i, fib_str_buf);
    }

    for (int i = offset; i >= 0; i--) {
        lseek(fd, i, SEEK_SET);
        sz = read(fd, &bn_buf, sizeof(bn_buf));
        toString_bigN(bn_buf, fib_str_buf, sizeof(fib_str_buf));
        printf("Reading from " FIB_DEV
               " at offset %d, returned the sequence "
               "%s.\n",
               i, fib_str_buf);
    }

    close(fd);
    return 0;
}
