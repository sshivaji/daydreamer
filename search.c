
#include <stdio.h>
#include <strings.h>
#include "daydreamer.h"

#define OUTPUT_DELAY    2000

search_data_t root_data;

static bool root_search(search_data_t* search_data);
static int quiesce(position_t* pos,
        search_node_t* search_node,
        int ply,
        int alpha,
        int beta,
        int depth);


void init_search_data(void)
{
    position_t copy;
    copy_position(&copy, &root_data.root_pos);
    memset(&root_data, 0, sizeof(root_data));
    copy_position(&root_data.root_pos, &copy);
    init_timer(&root_data.timer);
}

static void print_hash_pv(position_t* pos, int depth)
{
    int alpha, beta;
    move_t hash_move;
    undo_info_t undo;
    if (!depth ||
            !get_transposition(pos, depth, &alpha, &beta, &hash_move)) return;
    if (!is_move_legal(pos, hash_move)) return;
    print_la_move(hash_move);
    do_move(pos, hash_move, &undo);
    print_hash_pv(pos, depth-1);
    undo_move(pos, hash_move, &undo);
}

static bool should_stop_searching(search_data_t* data)
{
    if (data->engine_status == ENGINE_ABORTED) return true;
    if (!data->infinite &&
            data->time_target &&
            elapsed_time(&data->timer) >= data->time_target) return true;
    // TODO: take time_limit and search difficulty into account
    if (data->node_limit &&
            data->nodes_searched >= data->node_limit) return true;
    // TODO: we need a heuristic for when the current result is "good enough",
    // regardless of search params.
    return false;
}

static bool should_deepen(search_data_t* data)
{
    if (should_stop_searching(data)) return false;
    // If we're more than halfway through our time, we won't make it through
    // the next iteration anyway. TODO: this margin could be tightened up.
    if (!data->infinite && data->time_target &&
        data->time_target-elapsed_time(&data->timer) <
        data->time_target/2) return false;
    return true;
}

void perform_periodic_checks(search_data_t* data)
{
    if (should_stop_searching(data)) data->engine_status = ENGINE_ABORTED;
    check_for_input(data);
}

static bool is_nullmove_allowed(position_t* pos)
{
    // don't allow nullmove if either side is in check
    if (is_square_attacked(pos,
            pos->pieces[pos->side_to_move][KING][0].location,
            pos->side_to_move^1) ||
        is_square_attacked(pos,
            pos->pieces[pos->side_to_move^1][KING][0].location,
            pos->side_to_move)) return false;
    // allow nullmove if we're not down to king/pawns
    int piece_value = pos->material_eval[pos->side_to_move] -
        material_value(WK) -
        material_value(WP)*pos->piece_count[pos->side_to_move][PAWN];
    return piece_value != 0;
}

int search(position_t* pos,
        search_node_t* search_node,
        int ply,
        int alpha,
        int beta,
        int depth)
{
    if (root_data.engine_status == ENGINE_ABORTED) return 0;
    if (alpha > MATE_VALUE - ply - 1) return alpha; // can't beat this
    if (depth <= 0) {
        search_node->pv[ply] = NO_MOVE;
        return quiesce(pos, search_node, ply, alpha, beta, depth);
    }
    if (is_draw(pos)) return DRAW_VALUE;
    if ((++root_data.nodes_searched & POLL_INTERVAL) == 0) {
        perform_periodic_checks(&root_data);
    }
    bool full_window = (beta-alpha > 1);

    // check transposition table
    if (!full_window) { // TODO: use hash for move ordering
        move_t hash_move;
        bool hash_hit = get_transposition(pos, depth, &alpha, &beta, &hash_move);
        if (hash_hit && alpha >= beta) {
            search_node->pv[ply] = hash_move;
            search_node->pv[ply+1] = 0;
            return alpha;
        }
    }

    bool pv = true;
    int score = -MATE_VALUE-1;
    move_t moves[256];
    // nullmove reduction, just check for beta cutoff
    if (is_nullmove_allowed(pos)) {
        undo_info_t undo;
        do_nullmove(pos, &undo);
        score = -search(pos, search_node+1, ply+1,
                -beta, -beta+1, depth-NULL_R);
        undo_nullmove(pos, &undo);
        if (score >= beta) {
            depth -= NULLMOVE_DEPTH_REDUCTION;
            if (depth <= 0) {
                //return simple_eval(pos);
                return quiesce(pos, search_node, ply, alpha, beta, depth);
            } else {
                return beta;
            }
        }
    }
    generate_pseudo_moves(pos, moves);
    int num_legal_moves = 0;
    int orig_alpha = alpha;
    for (move_t* move = moves; *move; ++move) {
        if (!is_move_legal(pos, *move)) continue;
        ++num_legal_moves;
        undo_info_t undo;
        do_move(pos, *move, &undo);
        if (pv) score = -search(pos, search_node+1, ply+1,
                -beta, -alpha, depth-1);
        else {
            score = -search(pos, search_node+1, ply+1,
                    -alpha-1, -alpha, depth-1);
            if (score > alpha && score < beta) {
                score = -search(pos, search_node+1, ply+1,
                        -beta, -alpha, depth-1);
            }
        }
        undo_move(pos, *move, &undo);
        if (score >= beta) {
            // TODO: killer move heuristic
            put_transposition(pos, *move, depth, beta, SCORE_LOWERBOUND);
            return beta;
        }
        if (score > alpha) {
            alpha = score;
            pv = false;
            // update pv from child search nodes
            search_node->pv[ply] = *move;
            int i = ply;
            do {
                ++i;
                search_node->pv[i] = (search_node+1)->pv[i];
            } while ((search_node+1)->pv[i] != NO_MOVE);
        }
    }
    if (!num_legal_moves) {
        // No legal moves, this is either stalemate or checkmate.
        search_node->pv[ply] = NO_MOVE;
        if (is_check(pos)) {
            // note: adjust MATE_VALUE by ply so that we favor shorter mates
            return -(MATE_VALUE-ply);
        }
        return DRAW_VALUE;
    }
    score_type_t score_type = (alpha == orig_alpha) ?
        SCORE_UPPERBOUND : SCORE_EXACT;
    put_transposition(pos, search_node->pv[ply], depth, alpha, score_type);
    return alpha;
}

// TODO: split up root search a bit
void deepening_search(search_data_t* search_data)
{
    search_data->engine_status = ENGINE_THINKING;
    init_timer(&search_data->timer);
    start_timer(&search_data->timer);
    // If |search_data| already has a list of root moves, we search only
    // those moves. Otherwise, search everything. This allows support for the
    // uci searchmoves command.
    if (!*search_data->root_moves) {
        generate_legal_moves(&search_data->root_pos, search_data->root_moves);
    }

    // iterative deepening loop
    root_data.best_score = -MATE_VALUE-1;
    for (search_data->current_depth=1;
            !search_data->depth_limit ||
            search_data->current_depth <= search_data->depth_limit;
            ++search_data->current_depth) {
        if (elapsed_time(&search_data->timer) > OUTPUT_DELAY) {
            print_transposition_stats();
            printf("info depth %d\n", search_data->current_depth);
        }
        bool finished = root_search(search_data);
        if (!finished || !should_deepen(search_data)) {
            ++search_data->current_depth;
            break;
        }
    }
    stop_timer(&search_data->timer);
    
    --search_data->current_depth;
    print_pv(search_data->pv, search_data->current_depth,
            search_data->best_score,
            elapsed_time(&search_data->timer),
            search_data->nodes_searched);
    printf("hashpv ");
    print_hash_pv(&search_data->root_pos, search_data->current_depth);
    printf("info string targettime %d elapsedtime %d\n",
            search_data->time_target, elapsed_time(&search_data->timer));
    print_transposition_stats();
    char la_move[6];
    move_to_la_str(search_data->best_move, la_move);
    printf("bestmove %s\n", la_move);
    root_data.engine_status = ENGINE_IDLE;
}

static bool root_search(search_data_t* search_data)
{
    int alpha = -MATE_VALUE-1, beta = MATE_VALUE+1;
    int best_depth_score = -MATE_VALUE-1;
    bool pv = true;
    int best_index = 0, move_index = 0;
    position_t* pos = &search_data->root_pos;
    for (move_t* move=search_data->root_moves; *move;
            ++move, ++move_index) {
        if (elapsed_time(&search_data->timer) > OUTPUT_DELAY) {
            char la_move[6];
            move_to_la_str(*move, la_move);
            printf("info currmove %s currmovenumber %d\n", la_move, move_index);
        }
        undo_info_t undo;
        do_move(pos, *move, &undo);
        int score;
        if (pv) {
            score = -search(pos, search_data->search_stack,
                    1, -beta, -alpha, search_data->current_depth-1);
        } else {
            score = -search(pos, search_data->search_stack,
                    1, -alpha-1, -alpha, search_data->current_depth-1);
            if (score > alpha) {
                score = -search(pos, search_data->search_stack,
                    1, -beta, -alpha, search_data->current_depth-1);
            }
        }
        undo_move(pos, *move, &undo);
        if (search_data->engine_status == ENGINE_ABORTED) return false;
        // update score
        if (score > alpha) {
            alpha = score;
            pv = false;
            if (score > best_depth_score) {
                best_depth_score = score;
                if (score > search_data->best_score ||
                        *move == search_data->best_move) {
                    search_data->best_score = score;
                    search_data->best_move = *move;
                    best_index = move_index;
                }
            }
            // update pv
            search_data->pv[0] = *move;
            int i=1;
            for (; search_data->search_stack->pv[i] != NO_MOVE; ++i) {
                search_data->pv[i] = search_data->search_stack->pv[i];
            }
            search_data->pv[i] = NO_MOVE;
            if (elapsed_time(&search_data->timer) > OUTPUT_DELAY) {
                print_pv(search_data->pv, search_data->current_depth,
                        search_data->best_score,
                        elapsed_time(&search_data->timer),
                        search_data->nodes_searched);
                printf("hashpv ");
                print_hash_pv(&search_data->root_pos,
                        search_data->current_depth);
                printf("\n");
            }
        }
    }
    if (alpha != -MATE_VALUE-1) {
        // swap the pv move to the front of the list
        search_data->root_moves[best_index] = search_data->root_moves[0];
        search_data->root_moves[0] = search_data->best_move;
        search_data->best_score = best_depth_score;
        put_transposition(pos, search_data->best_move,
                search_data->current_depth,
                search_data->best_score, SCORE_EXACT);
    }
    return true;
}

static int quiesce(position_t* pos,
        search_node_t* search_node,
        int ply,
        int alpha,
        int beta,
        int depth)
{
    if (root_data.engine_status == ENGINE_ABORTED) return 0;
    if ((++root_data.nodes_searched & POLL_INTERVAL) == 0) {
        perform_periodic_checks(&root_data);
    }
    if (alpha > MATE_VALUE - ply - 1) return alpha; // can't beat this
    int eval = simple_eval(pos);
    int score = eval;
    if (score >= beta) return beta;
    if (alpha < score) alpha = score;
    
    move_t moves[256];
    generate_pseudo_captures(pos, moves);
    int num_legal_captures = 0;
    for (move_t* move = moves; *move; ++move) {
        if (!is_move_legal(pos, *move)) continue;
        ++num_legal_captures;
        if (static_exchange_eval(pos, *move) < 0) continue;
        undo_info_t undo;
        do_move(pos, *move, &undo);
        score = -quiesce(pos, search_node+1, ply+1, -beta, -alpha, depth-1);
        undo_move(pos, *move, &undo);
        if (score >= beta) return beta;
        if (score > alpha) {
            alpha = score;
            // update pv from child search nodes
            search_node->pv[ply] = *move;
            int i = ply;
            do {
                ++i;
                search_node->pv[i] = (search_node+1)->pv[i];
            } while ((search_node+1)->pv[i] != NO_MOVE);
        }
    }
    if (!num_legal_captures) {
        search_node->pv[ply] = NO_MOVE;
        int num_legal_noncaptures = generate_legal_noncaptures(pos, moves);
        if (num_legal_noncaptures) {
            // TODO: if we're in check, we haven't quiesced yet--handle this
            // we've reached quiescence
            return eval;
        }
        // No legal moves, this is either stalemate or checkmate.
        // note: adjust MATE_VALUE by ply so that we favor shorter mates
        if (is_check(pos)) {
            return -(MATE_VALUE-ply);
        }
        return DRAW_VALUE;
    }
    return alpha;
}

/*
 * Basic minimax search, no pruning or cleverness of any kind. Used strictly
 * for debugging.
 */
static int minimax(position_t* pos,
        search_node_t* search_node,
        int ply,
        int depth)
{
    if (root_data.engine_status == ENGINE_ABORTED) return 0;
    if (!depth) {
        search_node->pv[ply] = NO_MOVE;
        return simple_eval(pos);
    }
    if ((++root_data.nodes_searched & POLL_INTERVAL) == 0) {
        perform_periodic_checks(&root_data);
    }
    int score, best_score = -MATE_VALUE-1;
    move_t moves[256];
    int num_moves = generate_legal_moves(pos, moves);
    for (move_t* move = moves; *move; ++move) {
        undo_info_t undo;
        do_move(pos, *move, &undo);
        score = -minimax(pos, search_node+1, ply+1, depth-1);
        undo_move(pos, *move, &undo);
        if (score > best_score) {
            best_score = score;
            // update pv from child search nodes
            search_node->pv[ply] = *move;
            int i = ply;
            do {
                ++i;
                search_node->pv[i] = (search_node+1)->pv[i];
            } while ((search_node+1)->pv[i] != NO_MOVE);
        }
    }
    if (!num_moves) {
        // No legal moves, this is either stalemate or checkmate.
        search_node->pv[ply] = NO_MOVE;
        // note: adjust MATE_VALUE by ply so that we favor shorter mates
        if (is_check(pos)) {
            return -(MATE_VALUE-ply);
        }
        return DRAW_VALUE;
    }
    return best_score;
}

/*
 * Full minimax tree search. As slow and accurate as possible. Used to debug
 * more sophisticated search strategies.
 */
void root_search_minimax(void)
{
    position_t* pos = &root_data.root_pos;
    int depth = root_data.depth_limit;
    root_data.engine_status = ENGINE_THINKING;
    init_timer(&root_data.timer);
    start_timer(&root_data.timer);
    if (!*root_data.root_moves) generate_legal_moves(pos, root_data.root_moves);

    root_data.best_score = -MATE_VALUE-1;
    for (move_t* move=root_data.root_moves; *move; ++move) {
        undo_info_t undo;
        do_move(pos, *move, &undo);
        int score = -minimax(pos, root_data.search_stack, 1, depth-1);
        undo_move(pos, *move, &undo);
        // update score
        if (score > root_data.best_score) {
            root_data.best_score = score;
            root_data.best_move = *move;
            // update pv
            root_data.pv[0] = *move;
            int i=1;
            for (; root_data.search_stack->pv[i] != NO_MOVE; ++i) {
                root_data.pv[i] = root_data.search_stack->pv[i];
            }
            root_data.pv[i] = NO_MOVE;
            print_pv(root_data.pv, depth,
                    root_data.best_score,
                    elapsed_time(&root_data.timer),
                    root_data.nodes_searched);
        }
    }
        
    stop_timer(&root_data.timer);
    printf("info string targettime %d elapsedtime %d\n",
            root_data.time_target, elapsed_time(&root_data.timer));
    print_pv(root_data.pv, depth,
            root_data.best_score,
            elapsed_time(&root_data.timer),
            root_data.nodes_searched);
    char la_move[6];
    move_to_la_str(root_data.best_move, la_move);
    printf("bestmove %s\n", la_move);
    root_data.engine_status = ENGINE_IDLE;
}

