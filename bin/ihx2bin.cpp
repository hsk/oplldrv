// 参照ファイルのインクルード
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>


// 内部関数の宣言
static int strtoint(const char **s, int digit);


// メインプログラムのエントリ
//
int main(int argc, const char *argv[])
{
    char bload = 1;
    // 入力ファイル名の初期化
    const char *inname = NULL;
    
    // 出力ファイル名の初期化
    const char *outname = NULL;
    
    // 引数の取得
    while (--argc > 0) {
        ++argv;
        if (strcasecmp(*argv, "-o") == 0) {
            outname = *++argv;
            --argc;
        } else if (strcasecmp(*argv, "-n") == 0) {
            bload = 0;
        } else if (strcasecmp(*argv, "-b") == 0) {
            bload = 2;
        } else {
            inname = *argv;
        }
    }
    
    // 入力／出力ファイルがない
    if (inname == NULL || outname == NULL) {
        return -1;
    }

    // バッファの初期化
    unsigned char buffer[0x10000];
    int bufferhead = 0x10000;
    int buffertail = 0x00000;
    for (int i = 0x00000; i < 0x10000; i++) {
        buffer[i] = 0x00;
    }
    
    // 入力ファイルを開く
    FILE *infile = fopen(inname, "r");
    
    // 出力ファイルを開く
    FILE *outfile = fopen(outname, "wb");
    
    // ファイルの読み込み
    char line[1024];
    while (fgets(line, 1024, infile) != NULL) {

        // スタートコードのチェック
        const char *s = line;
        if (*s++ == ':') {

            // バイトカウントの取得
            int bytecount = strtoint(&s, 2);

            // アドレスの取得
            int address = strtoint(&s, 4);

            // レコードタイプの取得
            int recordtype = strtoint(&s, 2);
            if (recordtype == 0x00) {

                // アドレスのチェック
                if (bufferhead > address) {
                    bufferhead = address;
                }
                if (buffertail < address + bytecount) {
                    buffertail = address + bytecount;
                }

                // データの格納
                while (bytecount-- > 0) {
                    buffer[address++] = strtoint(&s, 2);
                }
            }
        }
    }

    switch (bload) {
    case 1:
        // bload ヘッダの出力
        fputc(bufferhead & 0x00ff, outfile);
        fputc((bufferhead >> 8) & 0x00ff, outfile);
        break;
    case 2:
        fputc(0xfe,outfile);
        fputc(bufferhead & 0x00ff, outfile);
        fputc((bufferhead >> 8) & 0x00ff, outfile);
        fputc(buffertail & 0x00ff, outfile);
        fputc((buffertail >> 8) & 0x00ff, outfile);
        fputc(bufferhead & 0x00ff, outfile);
        fputc((bufferhead >> 8) & 0x00ff, outfile);
        break;
    }
    
    // データの出力
    for (int i = bufferhead; i < buffertail; i++) {
        fputc(buffer[i], outfile);
    }

    // 出力ファイルを閉じる
    if (outfile != stdout) {
        fclose(outfile);
    }
    
    // 入力ファイルを閉じる
    fclose(infile);

    // 結果の出力
    fprintf(stdout, "%s: %04X-%04X\n", outname, bufferhead, buffertail - 1);

    // 終了
    return 0;
}

// 文字列の数値変換
//
static int strtoint(const char **s, int digit)
{
    int value = 0;
    while (digit-- > 0) {
        value <<= 4;
        char c = **s;
        if ('0' <= c && c <= '9') {
            value += c - '0';
        } else if ('A' <= c && c <= 'F') {
            value += c - 'A' + 0x0a;
        } else if ('a' <= c && c <= 'f') {
            value += c - 'a' + 0x0a;
        }
        (*s)++;
    }
    return value;
}
