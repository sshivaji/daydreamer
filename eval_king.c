
#include "daydreamer.h"

const int shield_value[2][17] = {
    { 0, 8, 2, 4, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 2, 4, 1, 1, 0, 0, 0 },
};

const int king_attack_score[16] = {
    0, 5, 20, 20, 30, 50, 0, 0, 0, 5, 20, 20, 30, 50, 0, 0
};
const int multiple_king_attack_scale[16] = {
    0, 0, 512, 640, 896, 960, 1024, 1024,
    1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024
};

/*
 * Give some points for pawns directly in front of your king.
 */
static int king_shield_score(const position_t* pos, color_t side, square_t king)
{
    int s = 0;
    int push = pawn_push[side];
    s += shield_value[side][pos->board[king-1]] * 2;
    s += shield_value[side][pos->board[king+1]] * 2;
    s += shield_value[side][pos->board[king+push-1]] * 4;
    s += shield_value[side][pos->board[king+push]] * 6;
    s += shield_value[side][pos->board[king+push+1]] * 4;
    s += shield_value[side][pos->board[king+2*push-1]];
    s += shield_value[side][pos->board[king+2*push]] * 2;
    s += shield_value[side][pos->board[king+2*push+1]];
    return s;
}

/*
 * Compute the overall balance of king safety offered by pawn shields.
 */
score_t evaluate_king_shield(const position_t* pos)
{
    score_t phase_score;
    phase_score.midgame = phase_score.endgame = 0;
    //if (pos->piece_count[BQ] + pos->piece_count[WQ] == 0) return phase_score;
    int score[2] = {0, 0};
    int oo_score[2] = {0, 0};
    int ooo_score[2] = {0, 0};
    int castle_score[2] = {0, 0};
    score[WHITE] = king_shield_score(pos, WHITE, pos->pieces[WHITE][0]);
    if (has_oo_rights(pos, WHITE)) {
        oo_score[WHITE] = king_shield_score(pos, WHITE, G1);
    }
    if (has_ooo_rights(pos, WHITE)) {
        ooo_score[WHITE] = king_shield_score(pos, WHITE, C1);
    }
    castle_score[WHITE] = MAX(score[WHITE],
        MAX(oo_score[WHITE], ooo_score[WHITE]));
    score[BLACK] = king_shield_score(pos, BLACK, pos->pieces[BLACK][0]);
    if (has_oo_rights(pos, BLACK)) {
        oo_score[BLACK] = king_shield_score(pos, BLACK, G8);
    }
    if (has_ooo_rights(pos, WHITE)) {
        ooo_score[BLACK] = king_shield_score(pos, BLACK, C8);
    }
    castle_score[BLACK] = MAX(score[BLACK],
        MAX(oo_score[BLACK], ooo_score[BLACK]));
    score[WHITE] = (score[WHITE] + castle_score[WHITE]) / 2;
    score[BLACK] = (score[BLACK] + castle_score[BLACK]) / 2;
    color_t side = pos->side_to_move;
    phase_score.midgame = score[side]-score[side^1];
    return phase_score;
}

/*
 * Compute a measure of king safety given by the number and type of pieces
 * attacking a square adjacent to the king.
 * TODO: This could probably be a lot more sophisticated.
 */
score_t evaluate_king_attackers(const position_t* pos)
{
    int score[2] = {0, 0};
    for (color_t side = WHITE; side <= BLACK; ++side) {
        if (pos->piece_count[create_piece(side, QUEEN)] == 0) continue;
        const square_t opp_king = pos->pieces[side^1][0];
        int num_attackers = 0;
        for (int i=1; i<pos->num_pieces[side]; ++i) {
            const square_t attacker = pos->pieces[side][i];
            if (piece_attacks_near(pos, attacker, opp_king)) {
                score[side] += king_attack_score[pos->board[attacker]];
                num_attackers++;
            }
        }
        score[side] = score[side] *
            multiple_king_attack_scale[num_attackers] / 1024;
    }
    color_t side = pos->side_to_move;
    score_t phase_score;
    phase_score.midgame = score[side]-score[side^1];
    phase_score.endgame = 0;
    return phase_score;
}
