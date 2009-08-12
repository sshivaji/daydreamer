
#ifndef SEARCH_H
#define SEARCH_H
#ifdef __cplusplus
extern "C" {
#endif

#define MAX_SEARCH_DEPTH        64

typedef struct {
    move_t pv[MAX_SEARCH_DEPTH];
    move_t killers[2];
} search_node_t;

typedef enum {
    ENGINE_IDLE=0, ENGINE_PONDERING, ENGINE_THINKING, ENGINE_ABORTED
} engine_status_t;

typedef enum {
    SCORE_EXACT, SCORE_LOWERBOUND, SCORE_UPPERBOUND
} score_type_t;

typedef struct {
    int poll_interval;
    int output_delay;
    bool enable_pv_iid;
    bool enable_non_pv_iid;
    int iid_pv_depth_reduction;
    int iid_non_pv_depth_reduction;
    int iid_pv_depth_cutoff;
    int iid_non_pv_depth_cutoff;
} search_options_t;

#define HIST_BUCKETS    15

typedef struct {
    int cutoffs[MAX_SEARCH_DEPTH];
    int move_selection[HIST_BUCKETS + 1];
    int razor_attempts[3];
    int razor_prunes[3];
} search_stats_t;

typedef struct {
    position_t root_pos;
    search_options_t options;
    search_stats_t stats;

    // search state info
    move_t root_moves[256];
    move_t best_move; // FIXME: shouldn't this be redundant with pv[0]?
    int best_score;
    move_t pv[MAX_SEARCH_DEPTH];
    search_node_t search_stack[MAX_SEARCH_DEPTH];
    uint64_t nodes_searched;
    uint64_t qnodes_searched;
    int current_depth;
    engine_status_t engine_status;

    // when should we stop?
    milli_timer_t timer;
    uint64_t node_limit;
    int depth_limit;
    int time_limit;
    int time_target;
    int mate_search;
    bool infinite;
    bool ponder;
} search_data_t;

#define POLL_INTERVAL   0xffff
#define MATE_VALUE      0x7fff
#define DRAW_VALUE      0
#define NULL_R          4
#define NULLMOVE_VERIFICATION_REDUCTION    5
#define NULL_EVAL_MARGIN            300
#define RAZOR_DEPTH_LIMIT           3
#define FUTILITY_DEPTH_LIMIT        1
#define LMR_EARLY_MOVES             3
#define LMR_DEPTH_LIMIT             1
#define LMR_REDUCTION               1

#define is_mate_score(score)       (abs(score) + 256 > MATE_VALUE)
#define should_output(s)    \
    (elapsed_time(&((s)->timer)) > (s)->options.output_delay)


#ifdef __cplusplus
} // extern "C"
#endif
#endif // SEARCH_H
