#include <algorithm>
#include <cassert>
#include <cstdbool>
#include <cstdio>
#include <iterator>
#include <filesystem>
#include <iostream>
#include <regex>
#include <sstream>

#include "../common/util.h"
#include "MNQueues.h"


MNQueues::MNQueues(int argc, char* argv[])
{
    parse_and_validate_args(argc, argv);

    // set some derived variables
    bits_per_line = line_size * 8;
    bits_per_page = page_size * 8;
    bits_per_node = n_bytes_mem_per_node * 8;
    line_size_log2 = __builtin_ctzll(line_size);
    page_size_log2 = __builtin_ctzll(page_size);
    bucket_cap = bits_per_node * cell_write_endurance;
    bucket_interval = bucket_cap / n_buckets;
    // one job per node
    n_nodes = jobs.size();

    // now, we can fill out jobs' bit_writes_per_quanta field
    for (auto& j : jobs) {
        j.bit_writes_per_quanta = (uint64_t)
                (scheduler_quanta_s * j.write_bw_bytes_s * 8 * j.write_factor);
        printf("BWPQ: %zu\n", j.bit_writes_per_quanta);
    }


    printf("n. buckets: %zu\n", n_buckets);
    printf("bucket interval: %zu\n", bucket_interval);
    printf("bucket cap: %zu\n", bucket_cap);
    printf("n. nodes: %zu\n", n_nodes);
    printf("scheduler time quanta: %.2fs\n", scheduler_quanta_s);

    if (bucket_interval < bits_per_node)
        print_message_and_die("bucket interval must be >= bits per node to "
                "avoid skipping buckets");

    if (n_promotions_to_event_trace != 0) {
        event_trace = std::make_unique<std::ofstream>(
                "mnqueues-promotion-timestamps-float64.bin",
                std::ofstream::out | std::ofstream::binary);
    }

    // preallocate space in some data structures
    queues_vec.resize(n_buckets);
}


MNQueues::~MNQueues()
{
    // free all entries in the queues
    for (auto& q : queues_vec) {
        for (auto& f : q) {
            delete f;
        }
    }
}


/*
 * Helper method that parses the jobs string into a vector of structs.
 */
std::vector<MNQueues::job_t>
MNQueues::parse_jobs_str(const std::string& jobs_str)
{
    std::vector<job_t> j_vec;

    std::regex comma_reg("\\,");
    std::regex colon_reg("\\:");

    std::sregex_token_iterator jtok_it(jobs_str.begin(), jobs_str.end(),
            comma_reg, -1);
    std::sregex_token_iterator jtok_end;
    std::vector<std::string> jtok(jtok_it, jtok_end);

    if (jtok.size() > std::numeric_limits<job_id_t>::max())
        print_message_and_die("exceeded max job count of %d\n",
                std::numeric_limits<job_id_t>::max());

    for (job_id_t i = 0; i < jtok.size(); ++i) {
        auto& jt = jtok[i];
        std::sregex_token_iterator bw_rss_wf_it(jt.begin(), jt.end(), colon_reg,
                -1);
        std::sregex_token_iterator bw_rss_wf_end;
        std::vector<std::string> bw_rss_wf(bw_rss_wf_it, bw_rss_wf_end);

        // NOTE: we'll fill the j.bit_writes_per_quanta field later
        job_t j = {i, std::stod(bw_rss_wf[0]), std::stoull(bw_rss_wf[1]),
                std::stod(bw_rss_wf[2]), 0};
        j_vec.emplace_back(std::move(j));
    }

    // check that all write factors are in [0.0, 1.0]
    for (auto& j : j_vec) {
        if (!(0.0 <= j.write_factor and j.write_factor <= 1.0))
            print_message_and_die("write factor for jobs str. (-j) must be in "
                    "[0.0, 1.0]");
    }

    return j_vec;
}


void
MNQueues::parse_and_validate_args(int argc, char* argv[])
{
    int c;
    optind = 0; // global: clear previous getopt() state, if any
    opterr = 0; // global: don't explicitly warn on unrecognized args
    int n_args_parsed = 0;

    // sentinels
    n_buckets = 0;
    cell_write_endurance = 0;
    n_bytes_mem_per_node = 0;
    line_size = 0;
    page_size = 0;
    scheduler_quanta_s = 0.0;
    rebalance = -1;
    jobs_str = "";


    // parse
    while ((c = getopt(argc, argv, "n:c:l:p:i:e:g:t:r:j:")) != -1) {
        try {
            switch (c) {
                case 'n':
                    n_buckets = shorthand_to_integer(optarg, 1000);
                    break;
                case 'c':
                    cell_write_endurance = shorthand_to_integer(optarg, 1000);
                    break;
                case 'l':
                    line_size = shorthand_to_integer(optarg, 1024);
                    break;
                case 'p':
                    page_size = shorthand_to_integer(optarg, 1024);
                    break;
                case 'i':
                    n_iterations = shorthand_to_integer(optarg, 1000);
                    break;
                case 'e':
                    n_promotions_to_event_trace =
                            shorthand_to_integer(optarg, 1000);
                    break;
                case 'g':
                    n_bytes_mem_per_node = shorthand_to_integer(optarg, 1024);
                    break;
                case 't':
                    scheduler_quanta_s = std::stod(optarg);
                    break;
                case 'r':
                    rebalance = string_to_boolean(optarg);
                    break;
                case 'j':
                    jobs_str = optarg;
                    // also parse into the vector here
                    jobs = parse_jobs_str(jobs_str);
                    break;
                case '?':
                    print_message_and_die("unrecognized argument");
            }
        }
        catch (...) {
            print_message_and_die("generic arg parse failure");
        }
        ++n_args_parsed;
    }


    // and validate
    // the executable itself (1) plus each arg matched w/its preceding flag (*2)
    int argc_expected = 1 + (2 * n_args_parsed);
    if (argc != argc_expected)
        print_message_and_die("each argument must be accompanied by a flag");

    if (n_buckets == 0)
        print_message_and_die("must supply n. buckets (-n)");

    if (cell_write_endurance == 0)
        print_message_and_die("must supply cell write endurance (-c)");

    if (n_bytes_mem_per_node == 0)
        print_message_and_die("must supply requested memory size per node in "
                "bytes (-g)");

    if (__builtin_popcountll(n_bytes_mem_per_node) != 1) {
        print_message_and_die("requested memory size per node (-g) must be a "
                "power of two");
    }

    if (line_size == 0)
        print_message_and_die("must supply line size (-l)");

    if (page_size == 0)
        print_message_and_die("must supply page size (-p)");

    if (line_size > page_size)
        print_message_and_die("line size (-l) must be <= page size (-p)");

    if (__builtin_popcountll(line_size) != 1)
        print_message_and_die("line size (-l) must be a power of 2");

    if (__builtin_popcountll(page_size) != 1)
        print_message_and_die("page size (-p) must be a power of 2");

    if (scheduler_quanta_s == 0.0)
        print_message_and_die("must supply scheduler time quanta in seconds "
                "(-t)");

    if (rebalance == -1)
        print_message_and_die("must supply whether/not to perform rotation/"
                "rebalancing (-r)");

    if (jobs_str == "")
        print_message_and_die("must supply jobs str., of the form "
                "WBW0:WF0,WBW1:WF1,... (-j)");
}


void
MNQueues::run()
{
    if (rebalance) run_rebalance();
    else run_no_rebalance();
}


void
MNQueues::run_rebalance()
{
    // first, construct all nodes in the initial starting queues state
    for (node_id_t i = 0; i < n_nodes; ++i) {
        // allocate everything in the bottommost queue initially
        // job i maps to node i initially
        node_meta_t* nm = new node_meta_t{0, 0, 0, i};
        queues_vec[0].emplace_back(nm);
        auto lq_back = std::next(queues_vec[0].end(), -1);
        // and the job map
        job_map.emplace_back(lq_back);
    }

    // print some initial stats
    printf("Beginning simulation\n");
    printf("Global MiB in memory, per-node: %zu\n", n_bytes_mem_per_node /
            (1024 * 1024));


    // main loop
    bool cont = true;
    for (epoch = 0; epoch < n_iterations and cont; ++epoch) {

        // print some statistics
        if ((epoch + 1) % (uint64_t) 100000000 == 0)
            dump_stats(false /* incremental, not final */);

        for (auto& j : jobs) {
            // find out what node this job is currently mapped to
            auto nmi = job_map[j.idx];
            node_meta_t* nm = *nmi;

            if (nm->interval_bfs > bucket_interval) {
                // node has hit its write interval.
                // 1. promote the node into the next-higher queue
                // 2. in the lowest active queue, "rotate" the head node with
                //    the tail node
                // 3. swap the contents of the new tail node in the lowest
                //    queue with the promoted node
                // NOTE: we do account for extra writes incurred by swap

                size_t old_queue_idx = nm->queue;
                queues_vec[old_queue_idx].erase(nmi);
                size_t new_queue_idx = old_queue_idx + 1;

                // check to update the memoized lowest queue
                if (queues_vec[lowest_active_queue].empty())
                    lowest_active_queue += 1;

                // check if we've maxed out the queues
                if (new_queue_idx == queues_vec.size()) {
                    // break out of the loop and exit after this
                    cont = false;
                }
                else {
                    queues_vec[new_queue_idx].emplace_back(nm);
                    nm->queue = new_queue_idx;
                    // subtract off the bucket interval to indicate promotion
                    nm->interval_bfs -= bucket_interval;
                    //printf("q0l: %zu; promotion to %zu; ibfs: %zu\n",
                    //        queues_vec[0].size(), fm->queue, fm->interval_bfs);

                    // NOTE: we only do the swap to a lower bucket
                    // (never to same)
                    if (lowest_active_queue < nm->queue) {

                        // pop-and-push in the lowest active queue
                        auto lnm = queues_vec[lowest_active_queue].front();
                        queues_vec[lowest_active_queue].pop_front();
                        queues_vec[lowest_active_queue].emplace_back(lnm);

                        // swap job_idx in l/fm and page_map
                        nm->job_idx = lnm->job_idx;
                        lnm->job_idx = j.idx;

                        // update job_map to reflect the now-swapped mapping.
                        // both frames are now at the back of their respective
                        // queues.
                        auto new_queue_back =
                                std::next(queues_vec[new_queue_idx].end(), -1);
                        auto lowest_queue_back =
                                std::next(queues_vec[lowest_active_queue].end(),
                                -1);

                        job_map[nm->job_idx] = new_queue_back;
                        job_map[lnm->job_idx] = lowest_queue_back;

                        // apply the swap write itself to both nodes
                        // NOTE: technically, our "bit flip percentages" are
                        // defined only for successive time steps of writes of
                        // the same job onto a node, and undefined for
                        // "job 1" being remapped onto a node originally mapped
                        // by "job 0". However, we can approximate the remap
                        // bitflip as the *newly-mapped* job's bitflip value.
                        uint64_t lnm_rss_bytes = jobs[lnm->job_idx].rss_bytes;
                        uint64_t nm_rss_bytes = jobs[nm->job_idx].rss_bytes;
                        // note the switchover
                        uint64_t nm_swap_bfs = lnm_rss_bytes *
                                jobs[lnm->job_idx].write_factor;
                        uint64_t lnm_swap_bfs = nm_rss_bytes *
                                jobs[nm->job_idx].write_factor;
                        nm->interval_bfs += nm_swap_bfs;
                        nm->lifetime_bfs += nm_swap_bfs;
                        lnm->interval_bfs += lnm_swap_bfs;
                        lnm->lifetime_bfs += lnm_swap_bfs;

                        // increment the total bytes transferred, as well as
                        // as well as "total_bytes_delay", which counts the
                        // maximum of the two amounts transferred. this allows
                        // us to calculate a transfer delay (since the link is
                        // assumed to be full-duplex)
                        total_bytes_transferred += lnm_rss_bytes + nm_rss_bytes;
                        total_bytes_delay +=
                                std::max(lnm_rss_bytes, nm_rss_bytes);

                        ++total_n_promotions;
                    }
                }
            }
            else {
                nm->interval_bfs += jobs[nm->job_idx].bit_writes_per_quanta;
            }


            //// whether we hit interval or not, increment both bfs
            nm->lifetime_bfs += jobs[nm->job_idx].bit_writes_per_quanta;


            // always check to update the most-written node at end
            // nullptr check: ensure we always have some valid most_written_node
            if (most_written_node == nullptr or
                    nm->lifetime_bfs > most_written_node->lifetime_bfs) {
                most_written_node = nm;
            }
        }

        system_time_s += scheduler_quanta_s;

        // if we're within n_promotions_to_event_trace, trace the event
        // timestamp (system time in s)
        if (total_n_promotions < n_promotions_to_event_trace) {
            double curr_timestamp = system_time_s;
            event_trace.get()->write((char*) &curr_timestamp,
                    sizeof(curr_timestamp));
        }
    }
}


void
MNQueues::run_no_rebalance()
{
    // first, find the most write-intensive job
    // (argmax of jobs' bit_writes_per_quanta)
    uint64_t bwpq = 0;
    job_t* mj;
    for (auto& j : jobs) {
        if (bwpq < j.bit_writes_per_quanta) {
            mj = &j;
            bwpq = j.bit_writes_per_quanta;
        }
    }

    // now, we know mj is the most write-intensive job

    // how many scheduler_quantas will it take to exhaust a node's write budget?
    uint64_t n_quantas = (bits_per_node * cell_write_endurance) /
            mj->bit_writes_per_quanta;
    system_time_s = scheduler_quanta_s * n_quantas;
    epoch = n_quantas;
    lowest_active_queue = 0;

    no_rebalance_mwn.interval_bfs = n_quantas * mj->bit_writes_per_quanta;
    no_rebalance_mwn.lifetime_bfs = n_quantas * mj->bit_writes_per_quanta;
    // it was technically never promoted...
    no_rebalance_mwn.queue = 0;
    no_rebalance_mwn.job_idx = mj->idx;

    // so that mwn stats can be printed in dump_stats()
    most_written_node = &no_rebalance_mwn;

    // NOTE: LIFETIME_EST_VIAAVG will be undefined b/c of this, but that's fine
    // (we just use LIFETIME_EST_VIAMAX here anyway)
}


void
MNQueues::dump_stats(bool final)
{
    /*
     * NOTE: VIAMAX is calculated
     * via the most-written node,
     * whereas VIAAVG is calculated
     * via the average of bitflips across all nodes' memories.
     */

    // don't want to continuously calculate these, so just do it here
    double most_written_node_wear_pct =
            (double) most_written_node->lifetime_bfs / (double) bucket_cap;
    double lifetime_est_viamax_s = (double) system_time_s /
            (double) most_written_node_wear_pct;
    double lifetime_est_viamax_y = lifetime_est_viamax_s /
            ((double) 86400 * 365);


    // these are only calculated (and printed) upon termination
    double lifetime_est_viaavg_s = 0.0;
    double lifetime_est_viaavg_y = 0.0;
    if (final) {
        uint64_t bfs_possible = n_bytes_mem_per_node * 8 * cell_write_endurance
                * n_nodes;
        uint64_t bfs_performed = 0;

        for (auto& l : queues_vec) {
            for (auto& n_it : l) {
                bfs_performed += n_it->lifetime_bfs;
            }
        }

        double frac_bfs = (double) bfs_performed / (double) bfs_possible;
        lifetime_est_viaavg_s = system_time_s / frac_bfs;
        lifetime_est_viaavg_y = lifetime_est_viaavg_s /
                ((double) (86400 * 365));
    }


    std::string status = final ? "termination" : "incremental";
    std::cout << "-------------------- " << status << " stats print" <<
            " --------------------" << std::endl;

    // using a stringstream, dump to both file and stdout
    std::stringstream ss;

    // if in termination mode, add some extra information about our invocation
    if (final) {
        ss << "QUEUES" << " " << n_buckets << std::endl;
        ss << "CELL_WRITE_ENDURANCE" << " " << cell_write_endurance <<
                std::endl;
        ss << "PAGE_SIZE_BYTES" << " " << page_size << std::endl;
        ss << "N_NODES" << " " << n_nodes << std::endl;
        ss << "MEMORY_BYTES_PER_NODE" << " " << n_bytes_mem_per_node <<
                std::endl;
    }

    ss << "EPOCHS" << " " << epoch << std::endl;
    ss << "SYSTEM_TIME_S" << " " << system_time_s << std::endl;
    ss << "MOST_WRITTEN_NODE_PTR" << " " << most_written_node << std::endl;
    ss << "MOST_WRITTEN_NODE_BFS" << " " << most_written_node->lifetime_bfs
            << std::endl;
    ss << "MOST_WRITTEN_NODE_WEAR_PCT" << " " << most_written_node_wear_pct
            << std::endl;
    ss << "MOST_WRITTEN_NODE_QUEUE" << " " << most_written_node->queue
            << std::endl;
    ss << "LOWEST_ACTIVE_QUEUE" << " " << lowest_active_queue << std::endl;
    ss << "TOTAL_BYTES_TRANSFERRED" << " " << total_bytes_transferred
            << std::endl;
    ss << "TOTAL_BYTES_DELAY" << " " << total_bytes_delay << std::endl;
    ss << "TOTAL_N_PROMOTIONS" << " " << total_n_promotions << std::endl;
    ss << "LIFETIME_EST_VIAMAX_S" << " " << lifetime_est_viamax_s << std::endl;
    ss << "LIFETIME_EST_VIAMAX_Y" << " " << lifetime_est_viamax_y << std::endl;

    if (final) {
        ss << "LIFETIME_EST_VIAAVG_S" << " " << lifetime_est_viaavg_s
                << std::endl;
        ss << "LIFETIME_EST_VIAAVG_Y" << " " << lifetime_est_viaavg_y
                << std::endl;
    }


    std::cout << ss.rdbuf()->str();

    // if in termination mode, also dump to file
    if (final) {
        std::ofstream ofs("mnqueues.txt", std::ofstream::out);
        ofs << ss.rdbuf()->str();
    }
}


int
main(int argc, char* argv[])
{
    MNQueues mnq(argc, argv);

    mnq.run();
    mnq.dump_stats(true /* final = true; termination */);

    return 0;
}
