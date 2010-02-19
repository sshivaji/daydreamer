
#include "daydreamer.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

FILE* ctg_file = NULL;
FILE* cto_file = NULL;

typedef struct {
    int pad;
    int low;
    int high;
} page_bounds_t;

page_bounds_t page_bounds;

typedef struct {
    uint8_t buf[64];
    int buf_len;
} ctg_signature_t;

typedef struct {
    int num_moves;
    uint8_t moves[100];
    int total;
    int wins;
    int losses;
    int draws;
    int recommendation;
} ctg_entry_t;

typedef struct {
    int file_from;
    int file_to;
    int rank_from;
    int rank_to;
} ctg_move_t;

static move_t squares_to_move(position_t* pos, square_t from, square_t to);
static bool ctg_get_entry(position_t* pos, ctg_entry_t* entry);

void append_bits_reverse(ctg_signature_t* sig,
        uint8_t bits,
        int bit_position,
        int num_bits)
{
    uint8_t * sig_byte = &sig->buf[bit_position/8];
    int offset = bit_position % 8;
    for (int i=offset; i<num_bits+offset; ++i, bits>>=1) {
        if (bits & 1) *sig_byte |= 1 << (7-(i%8));
        //printf("%d", bits&1 ? 1 : 0);
        if (i%8 == 7) *(++sig_byte) = 0;
    }
    //printf(" ");
}

void print_signature(ctg_signature_t* sig)
{
    // Print bits
    printf("\n%d byte signature", sig->buf_len);
    for (int i=0; i<sig->buf_len; ++i) {
        if (i % 8 == 0) printf("\n");
        for (int j=0; j<8; ++j) {
            printf("%d", sig->buf[i] & (1<<(7-j)) ? 1 : 0);
        }
        printf(" ");
    }

    // Just print as chars
    for (int i=0; i<sig->buf_len; ++i) {
        if (i % 8 == 0) printf("\n");
        printf("%3d ", (char)sig->buf[i]);
    }
    printf("\n");
}

int32_t ctg_signature_to_hash(ctg_signature_t* sig)
{
    static const uint32_t hash_bits[64] = {
        0x3100d2bf, 0x3118e3de, 0x34ab1372, 0x2807a847,
        0x1633f566, 0x2143b359, 0x26d56488, 0x3b9e6f59,
        0x37755656, 0x3089ca7b, 0x18e92d85, 0x0cd0e9d8,
        0x1a9e3b54, 0x3eaa902f, 0x0d9bfaae, 0x2f32b45b,
        0x31ed6102, 0x3d3c8398, 0x146660e3, 0x0f8d4b76,
        0x02c77a5f, 0x146c8799, 0x1c47f51f, 0x249f8f36,
        0x24772043, 0x1fbc1e4d, 0x1e86b3fa, 0x37df36a6,
        0x16ed30e4, 0x02c3148e, 0x216e5929, 0x0636b34e,
        0x317f9f56, 0x15f09d70, 0x131026fb, 0x38c784b1,
        0x29ac3305, 0x2b485dc5, 0x3c049ddc, 0x35a9fbcd,
        0x31d5373b, 0x2b246799, 0x0a2923d3, 0x08a96e9d,
        0x30031a9f, 0x08f525b5, 0x33611c06, 0x2409db98,
        0x0ca4feb2, 0x1000b71e, 0x30566e32, 0x39447d31,
        0x194e3752, 0x08233a95, 0x0f38fe36, 0x29c7cd57,
        0x0f7b3a39, 0x328e8a16, 0x1e7d1388, 0x0fba78f5,
        0x274c7e7c, 0x1e8be65c, 0x2fa0b0bb, 0x1eb6c371
    };

    int32_t hash = 0;
    int16_t tmp = 0;
    for (int i=0; i<sig->buf_len; ++i) {
        int8_t byte = sig->buf[i];
        tmp += ((0x0f - (byte & 0x0f)) << 2) + 1;
        hash += hash_bits[tmp & 0x3f];
        tmp += ((0xf0 - (byte & 0xf0)) >> 2) + 1;
        hash += hash_bits[tmp & 0x3f];
    }
    return hash;
}

void position_to_ctg_signature(position_t* pos, ctg_signature_t* sig)
{
    // Note: initial byte is reserved for length and flags info
    memset(sig, 0, sizeof(ctg_signature_t));
    int bit_position = 8;
    uint8_t bits = 0, num_bits = 0;
    bool flip_board = pos->side_to_move == BLACK;
    color_t white = flip_board ? BLACK : WHITE;
    bool mirror_board = square_file(pos->pieces[white][0]) < FILE_E && pos->castle_rights == 0;
    piece_t flip_piece[] = { 0, BP, BN, BB, BR, BQ, BK, 0, 0, WP, WN, WB, WR, WQ, WK };
    for (int file=0; file<8; ++file) {
        for (int rank=0; rank<8; ++rank) {
            square_t sq = create_square(file, rank);
            if (flip_board) sq = mirror_rank(sq);
            if (mirror_board) sq = mirror_file(sq);
            piece_t piece = flip_board ? flip_piece[pos->board[sq]] : pos->board[sq];
            switch (piece) {
                case EMPTY: bits = 0x0;
                            num_bits = 1;
                            break;
                case WP: bits = 0x3;
                         num_bits = 3;
                         break;
                case BP: bits = 0x7;
                         num_bits = 3;
                         break;
                case WN: bits = 0x9;
                         num_bits = 5;
                         break;
                case BN: bits = 0x19;
                         num_bits = 5;
                         break;
                case WB: bits = 0x5;
                         num_bits = 5;
                         break;
                case BB: bits = 0x15;
                         num_bits = 5;
                         break;
                case WR: bits = 0xD;
                         num_bits = 5;
                         break;
                case BR: bits = 0x1D;
                         num_bits = 5;
                         break;
                case WQ: bits = 0x11;
                         num_bits = 6;
                         break;
                case BQ: bits = 0x31;
                         num_bits = 6;
                         break;
                case WK: bits = 0x1;
                         num_bits = 6;
                         break;
                case BK: bits = 0x21;
                         num_bits = 6;
                         break;
                default: assert(false);
            }
            append_bits_reverse(sig, bits, bit_position, num_bits);
            bit_position += num_bits;
        }
    }

    // Encode castling and en passant rights.
    int ep = -1;
    int flag_bit_length = 0;
    if (pos->ep_square) {
        ep = square_file(pos->ep_square);
        if (mirror_board) ep = 7 - ep;
        flag_bit_length = 3;
    }
    int castle = 0;
    if (has_oo_rights(pos, white)) castle += 4;
    if (has_ooo_rights(pos, white)) castle += 8;
    if (has_oo_rights(pos, white^1)) castle += 1;
    if (has_ooo_rights(pos, white^1)) castle += 2;
    if (castle) flag_bit_length += 4;
    uint8_t flag_bits = castle;
    if (ep != -1) {
        flag_bits <<= 3;
        for (int i=0; i<3; ++i, ep>>=1) if (ep&1) flag_bits |= (1<<(2-i));
    }

    //printf("\nflag bits: %d\n", flag_bits);
    //printf("bit_position: %d\n", bit_position%8);
    //printf("flag_bit_length: %d\n", flag_bit_length);

    // Insert padding so that flags fit at the end of the last byte.
    int pad_bits = 0;
    if (8-(bit_position % 8) < flag_bit_length) {
        //printf("padding byte\n");
        pad_bits = 8 - (bit_position % 8);
        append_bits_reverse(sig, 0, bit_position, pad_bits);
        bit_position += pad_bits;
    }

    pad_bits = 8 - (bit_position % 8) - flag_bit_length;
    if (pad_bits < 0) pad_bits += 8;
    //printf("padding %d bits\n", pad_bits);
    append_bits_reverse(sig, 0, bit_position, pad_bits);
    bit_position += pad_bits;
    append_bits_reverse(sig, flag_bits, bit_position, flag_bit_length);
    bit_position += flag_bit_length;
    sig->buf_len = (bit_position + 7) / 8;

    // Write header byte
    sig->buf[0] = ((uint8_t)(sig->buf_len));
    if (ep != -1) sig->buf[0] |= 1<<5;
    if (castle) sig->buf[0] |= 1<<6;
}

void init_ctg(char* filename)
{
    int name_len = strlen(filename);
    assert(filename[name_len-3] == 'c' &&
            filename[name_len-2] == 't' &&
            filename[name_len-1] == 'g');
    char fbuf[1024];
    strcpy(fbuf, filename);
    if (ctg_file) {
        assert(cto_file);
        fclose(ctg_file);
        fclose(cto_file);
    }
    ctg_file = fopen(fbuf, "r");
    fbuf[name_len-1] = 'o';
    cto_file = fopen(fbuf, "r");
    fbuf[name_len-1] = 'b';
    FILE* ctb_file = fopen(fbuf, "r");
    fbuf[name_len-1] = 'g';
    if (!ctg_file || !cto_file || !ctb_file) {
        printf("info string Couldn't load book %s\n", fbuf);
        return;
    }

    // Read out upper and lower page limits.
    fread(&page_bounds, 12, 1, ctb_file);
    page_bounds.low = ntohl(page_bounds.low);
    page_bounds.high = ntohl(page_bounds.high);
    assert(page_bounds.low <= page_bounds.high);
    fclose(ctb_file);
    //printf("low %d high %d\n", page_bounds.low, page_bounds.high);
}

bool ctg_get_page_index(int hash, int* page_index)
{
    uint32_t key = 0;
    for (int mask = 0; key <= (uint32_t)page_bounds.high; mask = (mask << 1) + 1) {
        key = (hash & mask) + mask;
        //printf("c=%d\n", key);
        if (key >= (uint32_t)page_bounds.low) {
            //printf("found entry with key=%d\n", key);
            fseek(cto_file, 16 + key*4, SEEK_SET);
            fread(page_index, 4, 1, cto_file);
            *page_index = ntohl(*page_index);
            if (*page_index >= 0) return true;
        }
    }
    //printf("didn't find entry\n");
    return false;
}

#define read_24(buf, pos)   ((buf[pos]<<16) + (buf[(pos)+1]<<8) + (buf[(pos)+2]))

bool ctg_lookup_entry(int page_index, ctg_signature_t* sig, ctg_entry_t* entry)
{
    uint8_t buf[4096];
    fseek(ctg_file, 4096*(page_index + 1), SEEK_SET);
    if (!fread(buf, 1, 4096, ctg_file)) return false;
    int num_positions = (buf[0]<<8) + buf[1];
    int pos = 4;
    //printf("found %d positions\n", num_positions);

    for (int i=0; i<num_positions; ++i) {
        int entry_size = buf[pos] % 32;
        //for (int j=0; j<sig->buf_len; ++j) printf("%d ", (char)buf[pos+j]);
        //printf("\n");
        bool equal = true;
        if (sig->buf_len != entry_size) equal = false;
        for (int j=0; j<sig->buf_len && equal; ++j) {
            if (buf[pos+j] != sig->buf[j]) equal = false;
        }
        if (!equal) {
            pos += entry_size + buf[pos+entry_size] + 33;
            continue;
        }
        // Found it, fill in the entry and return.
        pos += entry_size;
        entry_size = buf[pos];
        for (int j=1; j<entry_size; ++j) entry->moves[j-1] = buf[pos+j];
        entry->num_moves = (entry_size - 1)/2;
        pos += entry_size;
        entry->total = read_24(buf, pos);
        pos += 3;
        entry->losses = read_24(buf, pos);
        pos += 3;
        entry->wins = read_24(buf, pos);
        pos += 3;
        entry->draws = read_24(buf, pos);
        pos += 21;
        entry->recommendation = buf[pos];
        return true;
    }
    return false;
}

move_t byte_to_move(position_t* pos, uint8_t byte)
{
    //printf("\n<%d> <%d> <%d>\n", byte, (char)byte, (int)byte);
    const char piece_code[257] = "PNxQPQPxQBKxPBRNxxBKPBxxPxQBxBxxxRBQPxBPQQNxxPBQNQBxNxNQQQBQBxxx"
                                 "xQQxKQxxxxPQNQxxRxRxBPxxxxxxPxxPxQPQxxBKxRBxxxRQxxBxQxxxxBRRPRQR"
                                 "QRPxxNRRxxNPKxQQxxQxQxPKRRQPxQxBQxQPxRxxxRxQxRQxQPBxxRxQxBxPQQKx"
                                 "xBBBRRQPPQBPBRxPxPNNxxxQRQNPxxPKNRxRxQPQRNxPPQQRQQxNRBxNQQQQxQQx";
    const int piece_index[256]= {
        5, 2, 9, 2, 2, 1, 4, 9, 2, 2, 1, 9, 1, 1, 2, 1,
        9, 9, 1, 1, 8, 1, 9, 9, 7, 9, 2, 1, 9, 2, 9, 9,
        9, 2, 2, 2, 8, 9, 1, 3, 1, 1, 2, 9, 9, 6, 1, 1,
        2, 1, 2, 9, 1, 9, 1, 1, 2, 1, 1, 2, 1, 9, 9, 9,
        9, 2, 1, 9, 1, 1, 9, 9, 9, 9, 8, 1, 2, 2, 9, 9,
        1, 9, 1, 9, 2, 3, 9, 9, 9, 9, 9, 9, 7, 9, 9, 5,
        9, 1, 2, 2, 9, 9, 1, 1, 9, 2, 1, 0, 9, 9, 1, 2,
        9, 9, 2, 9, 1, 9, 9, 9, 9, 2, 1, 2, 3, 2, 1, 1,
        1, 1, 6, 9, 9, 1, 1, 1, 9, 9, 1, 1, 1, 9, 2, 1,
        9, 9, 2, 9, 1, 9, 2, 1, 1, 1, 1, 3, 9, 1, 9, 2,
        2, 9, 1, 8, 9, 2, 9, 9, 9, 2, 9, 2, 9, 2, 2, 9,
        2, 6, 1, 9, 9, 2, 9, 1, 9, 2, 9, 5, 2, 2, 1, 9,
        9, 1, 2, 1, 2, 2, 2, 7, 7, 2, 2, 6, 2, 1, 9, 4,
        9, 2, 2, 2, 9, 9, 9, 1, 2, 1, 1, 1, 9, 9, 5, 1,
        2, 1, 9, 2, 9, 1, 4, 1, 1, 1, 9, 4, 1, 1, 2, 1,
        2, 1, 9, 2, 2, 2, 0, 1, 2, 2, 2, 2, 9, 1, 2, 9
    };
    const int forward[256]= {
         1,-1, 9, 0, 1, 1, 1, 9, 0, 6,-1, 9, 1, 3, 0,-1,
         9, 9, 7, 1, 1, 5, 9, 9, 1, 9, 6, 1, 9, 7, 9, 9,
         9, 0, 2, 6, 1, 9, 7, 1, 5, 0,-2, 9, 9, 1, 1, 0,
        -2, 0, 5, 9, 2, 9, 1, 4, 4, 0, 6, 5, 5, 9, 9, 9,
         9, 5, 7, 9,-1, 3, 9, 9, 9, 9, 2, 5, 2, 1, 9, 9,
         6, 9, 0, 9, 1, 1, 9, 9, 9, 9, 9, 9, 1, 9, 9, 2,
         9, 6, 2, 7, 9, 9, 3, 1, 9, 7, 4, 0, 9, 9, 0, 7,
         9, 9, 7, 9, 0, 9, 9, 9, 9, 6, 3, 6, 1, 1, 3, 0,
         6, 1, 1, 9, 9, 2, 0, 5, 9, 9,-2, 1,-1, 9, 2, 0,
         9, 9, 1, 9, 3, 9, 1, 0, 0, 4, 6, 2, 9, 2, 9, 4,
         3, 9, 2, 1, 9, 5, 9, 9, 9, 0, 9, 6, 9, 0, 3, 9,
         4, 2, 6, 9, 9, 0, 9, 5, 9, 3, 9, 1, 0, 2, 0, 9,
         9, 2, 2, 2, 0, 4, 5, 1, 2, 7, 3, 1, 5, 0, 9, 1,
         9, 1, 1, 1, 9, 9, 9, 1, 0, 2,-2, 2, 9, 9, 1, 1,
        -1, 7, 9, 3, 9, 0, 2, 4, 2,-1, 9, 1, 1, 7, 1, 0,
         0, 1, 9, 2, 2, 1, 0, 1, 0, 6, 0, 2, 9, 7, 3, 9
    };
    const int left[256] = {
        -1, 2, 9,-2, 0, 0, 1, 9,-4,-6, 0, 9, 1,-3,-3, 2,
         9, 9,-7, 0,-1,-5, 9, 9, 0, 9, 0, 1, 9,-7, 9, 9,
         9,-7, 2,-6, 1, 9, 7, 1,-5,-6,-1, 9, 9,-1,-1,-1,
         1,-3,-5, 9,-1, 9,-2, 0, 4,-5,-6, 5, 5, 9, 9, 9,
         9,-5, 7, 9,-1,-3, 9, 9, 9, 9, 0, 5,-1, 0, 9, 9,
         0, 9,-6, 9, 1, 0, 9, 9, 9, 9, 9, 9,-1, 9, 9, 0,
         9,-6, 0, 7, 9, 9, 3,-1, 9, 0,-4, 0, 9, 9,-5,-7,
         9, 9, 7, 9,-2, 9, 9, 9, 9, 6, 0, 0,-1, 0, 3,-1,
         6, 0, 1, 9, 9, 1,-7, 0, 9, 9,-1,-1, 1, 9, 2,-7,
         9, 9,-1, 9, 0, 9,-1, 1,-3, 0, 0, 0, 9, 0, 9, 4,
         0, 9,-2, 0, 9, 0, 9, 9, 9,-2, 9, 6, 9,-4,-3, 9,
         0, 0, 6, 9, 9,-5, 9, 0, 9,-3, 9, 0,-5, 0,-1, 9,
         9,-2,-2, 2,-1, 0, 0, 1, 0, 0, 3, 0, 5,-2, 9, 0,
         9, 1,-2, 2, 9, 9, 9, 1,-6, 2, 1, 0, 9, 9, 1, 1,
        -2, 0, 9, 0, 9,-4, 0,-4, 0,-2, 9,-1, 0,-7, 1,-4,
        -7,-1, 9, 1, 0,-1, 0, 2,-1, 0,-3,-2, 9, 0, 3, 9
    };

    // Find the piece. Note: the board may be mirrored/flipped.
    bool flip_board = pos->side_to_move == BLACK;
    color_t white = flip_board ? BLACK : WHITE;
    bool mirror_board = square_file(pos->pieces[white][0]) < FILE_E && pos->castle_rights == 0;
    piece_t flip_piece[] = { 0, BP, BN, BB, BR, BQ, BK, 0, 0, WP, WN, WB, WR, WQ, WK };
    int file_from = -1, file_to = -1, rank_from = -1, rank_to = -1;

    // Handle castling.
    if (byte == 107) {
        file_from = 4;
        file_to = 6;
        rank_from = rank_to = flip_board ? 7 : 0;
        return squares_to_move(pos,
                create_square(file_from, rank_from),
                create_square(file_to, rank_to));
    }
    if (byte == 246) {
        file_from = 4;
        file_to = 2;
        rank_from = rank_to = flip_board ? 7 : 0;
        return squares_to_move(pos,
                create_square(file_from, rank_from),
                create_square(file_to, rank_to));
    }

    piece_t pc = NONE;
    char glyph = piece_code[byte];
    switch (piece_code[byte]) {
        case 'P': pc = WP; break;
        case 'N': pc = WN; break;
        case 'B': pc = WB; break;
        case 'R': pc = WR; break;
        case 'Q': pc = WQ; break;
        case 'K': pc = WK; break;
        default: printf("%d -> (%c)\n", byte, glyph); assert(false);
    }

    int nth_piece = piece_index[byte], piece_count = 0;
    //printf("finding %c number %d\n", glyph, nth_piece);
    bool found = false;
    for (int file=0; file<8 && !found; ++file) {
        for (int rank=0; rank<8 && !found; ++rank) {
            square_t sq = create_square(file, rank);
            if (flip_board) sq = mirror_rank(sq);
            if (mirror_board) sq = mirror_file(sq);
            piece_t piece = flip_board ? flip_piece[pos->board[sq]] : pos->board[sq];
            if (piece == pc) piece_count++;
            if (piece_count == nth_piece) {
                file_from = file;
                rank_from = rank;
                //printf("found on %c%d\n", file+'a', rank+1);
                found = true;
            }
        }
    }
    assert(found);

    file_to = file_from - left[byte];
    file_to = (file_to + 8) % 8;
    rank_to = rank_from + forward[byte];
    rank_to = (rank_to + 8) % 8;
    if (flip_board) {
        rank_from = 7-rank_from;
        rank_to = 7-rank_to;
    }
    if (mirror_board) {
        file_from = 7-file_from;
        file_to = 7-file_to;
    }
    return squares_to_move(pos,
            create_square(file_from, rank_from),
            create_square(file_to, rank_to));
}

static move_t squares_to_move(position_t* pos, square_t from, square_t to)
{
    move_t possible_moves[256];
    int num_moves = generate_legal_moves(pos, possible_moves);
    move_t move;
    for (int i=0; i<num_moves; ++i) {
        move = possible_moves[i];
        if (from == get_move_from(move) &&
                to == get_move_to(move) &&
                (get_move_promote(move) == NONE ||
                 get_move_promote(move) == QUEEN)) return move;
    }
    assert(false);
    return NO_MOVE;
}

int move_weight(position_t* pos, move_t move)
{
    undo_info_t undo;
    do_move(pos, move, &undo);
    ctg_entry_t entry;
    bool success = ctg_get_entry(pos, &entry);
    undo_move(pos, move, &undo);
    if (!success) return 0;

    float half_points = (2*entry.wins + entry.draws) + 1;
    float games = (entry.wins + entry.draws + entry.losses) + 1;
    int scale = entry.recommendation;
    float weight = half_points / games;
    int int_weight = (int)(weight * 100000);
    if (scale == 64) int_weight = 0;
    if (scale == 128) int_weight *= 128;
    printf("info string book move: ");
    print_coord_move(move);
    printf(" scale: %d weight: %d\n", scale, int_weight);
    return int_weight;
}

bool ctg_pick_move(position_t* pos, ctg_entry_t* entry, move_t* move)
{
    //printf("looking for %d moves\n", entry->num_moves);
    move_t moves[50];
    int weights[50];
    int total_weight = 0;
    for (int i=0; i<2*entry->num_moves; i += 2) {
        uint8_t byte = entry->moves[i];
        //printf("%d: byte move: %d\n", i, byte);
        move_t m = byte_to_move(pos, byte);
        total_weight += move_weight(pos, m);
        moves[i/2] = m;
        weights[i/2] = total_weight;
        if (move == NO_MOVE) break;
    }
    uint32_t choice = random();
    choice = ((choice<<16) + random()) % total_weight;
    printf("choice %d\n", choice);
    int i;
    for (i=0; choice >= (uint32_t)weights[i]; ++i) {}
    if (i >= entry->num_moves) {
        printf("i: %d\nchoice: %d\ntotal_weight: %d\nnum_moves: %d\n",
                i, choice, total_weight, entry->num_moves);
        assert(false);
    }
    *move = moves[i];
    printf("info string choice: ");
    print_coord_move(moves[i]);
    printf("\n");
    return true;
}

bool ctg_get_entry(position_t* pos, ctg_entry_t* entry)
{
    ctg_signature_t sig;
    position_to_ctg_signature(pos, &sig);
    //print_signature(&sig);
    int page_index, hash = ctg_signature_to_hash(&sig);
    //printf("position hash: %d\n", hash);
    if (!ctg_get_page_index(hash, &page_index)) return false;
    //printf("page index: %d\n", page_index);
    if (!ctg_lookup_entry(page_index, &sig, entry)) return false;
    return true;
}

move_t ctg_get_book_move(position_t* pos)
{
    move_t move;
    ctg_entry_t entry;
    if (!ctg_get_entry(pos, &entry)) return NO_MOVE;
    if (!ctg_pick_move(pos, &entry, &move)) return NO_MOVE;
    return move;
}
