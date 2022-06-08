#include <algorithm>
#include <cassert>
#include <cstdbool>
#include <cstdio>
#include <iterator>
#include <filesystem>
#include <iostream>
#include <sstream>

#include "../common/util.h"
#include "SNQueues.h"


SNQueues::SNQueues(int argc, char* argv[])
{
    parse_and_validate_args(argc, argv);

    read_bittrack_files();

    std::string memtrace_filepath = memtrace_directory + "/" + "memtrace.bin";
    mtr.load(memtrace_filepath);

    if (mtr.get_n_writes_in_trace() == 0)
        print_message_and_die("trace contains no writes; lifetime = infinity");

    // set some derived variables
    bucket_cap = bits_per_page * cell_write_endurance;
    bucket_interval = bucket_cap / n_buckets;

    n_pages_requested = n_bytes_requested / page_size;

    printf("n. buckets: %zu\n", n_buckets);
    printf("bucket interval: %zu\n", bucket_interval);
    printf("bucket cap: %zu\n", bucket_cap);
    printf("n. writes in trace: %zu\n", mtr.get_n_writes_in_trace());

    if (bucket_interval < bits_per_page)
        print_message_and_die("bucket interval must be >= bits per page to "
                "avoid skipping buckets");

    // if we're outputting a trace of promotion cycles, remember the last cycle
    // in the trace, so that we can scale by it as we loop through
    if (n_promotions_to_event_trace != 0) {
        trace_end_cycle = mtr.get_last_entry()->cycle;
        event_trace = std::make_unique<std::ofstream>(
                "snqueues-promotion-timestamps-uint64.bin",
                std::ofstream::out | std::ofstream::binary);
    }

    // preallocate space in some data structures
    queues_vec.resize(n_buckets);
}


SNQueues::~SNQueues()
{
    // free all entries in the queues
    for (auto& q : queues_vec) {
        for (auto& f : q) {
            delete f;
        }
    }
}


void
SNQueues::parse_and_validate_args(int argc, char* argv[])
{
    int c;
    optind = 0; // global: clear previous getopt() state, if any
    opterr = 0; // global: don't explicitly warn on unrecognized args
    int n_args_parsed = 0;

    // sentinels
    n_buckets = 0;
    cell_write_endurance = 0;
    bittrack_directory = "";
    memtrace_directory = "";
    write_factor_mode_str = "";
    write_factor_mode = WF_MODE_INVALID;
    trace_time_s = 0.0;
    n_bytes_requested = 0;
    line_size = 0;
    page_size = 0;
    line_size_log2 = 0;
    page_size_log2 = 0;

    // parse
    while ((c = getopt(argc, argv, "n:c:b:m:w:t:i:e:g:")) != -1) {
        try {
            switch (c) {
                case 'n':
                    n_buckets = shorthand_to_integer(optarg, 1000);
                    break;
                case 'c':
                    cell_write_endurance = shorthand_to_integer(optarg, 1000);
                    break;
                case 'b':
                    bittrack_directory = optarg;
                    break;
                case 'm':
                    memtrace_directory = optarg;
                    break;
                case 'w':
                    write_factor_mode_str = optarg;
                    std::transform(write_factor_mode_str.begin(),
                            write_factor_mode_str.end(),
                            write_factor_mode_str.begin(), ::tolower);

                    // av, average, avg, etc. all match
                    if (write_factor_mode_str.find("average") !=
                            std::string::npos) {
                        write_factor_mode = WF_MODE_AVERAGE;
                    }
                    if (write_factor_mode_str.find("avg") !=
                            std::string::npos) {
                        write_factor_mode = WF_MODE_AVERAGE;
                    }

                    // per, per-page, page, etc. all match
                    if (write_factor_mode_str.find("per") !=
                            std::string::npos) {
                        write_factor_mode = WF_MODE_PER_PAGE;
                    }
                    if (write_factor_mode_str.find("page") !=
                            std::string::npos) {
                        write_factor_mode = WF_MODE_PER_PAGE;
                    }
                    break;
                case 't':
                    trace_time_s = std::stod(optarg);
                    break;
                case 'i':
                    n_iterations = shorthand_to_integer(optarg, 1000);
                    break;
                case 'e':
                    n_promotions_to_event_trace =
                            shorthand_to_integer(optarg, 1000);
                    break;
                case 'g':
                    n_bytes_requested = shorthand_to_integer(optarg, 1024);
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

    if (bittrack_directory == "")
        print_message_and_die("must supply BitTrack input directory (-b)");

    if (memtrace_directory == "")
        print_message_and_die("must supply MemTrace input directory (-m)");

    if (write_factor_mode == WF_MODE_INVALID)
        print_message_and_die("must supply write factor mode "
                "(-w <average|perpage>)");

    if (trace_time_s == 0.0)
        print_message_and_die("must supply trace time duration in seconds "
                "(-t)");

    if (n_bytes_requested == 0)
        print_message_and_die("must supply requested memory size in bytes "
                "(-g)");

    if (__builtin_popcountll(n_bytes_requested) != 1) {
        print_message_and_die("requested memory size (-g) must be a power of "
                "two");
    }
}


/*
 * Read bittrack.txt and bittrack.bin from the supplied input directory.
 */
void
SNQueues::read_bittrack_files()
{
    std::string txt_filepath = bittrack_directory + "/" + "bittrack.txt";
    std::string bin_filepath = bittrack_directory + "/" + "bittrack.bin";

    // make sure that bittrack_directory is actually a directory
    std::filesystem::path dir_path(bittrack_directory);
    std::filesystem::path txt_path(txt_filepath);
    std::filesystem::path bin_path(bin_filepath);
    std::error_code ec;

    if (!std::filesystem::is_directory(dir_path, ec))
        print_message_and_die("must supply a valid BitTrack directory");

    // make sure that bittrack.{txt, bin} exist in the directory
    if (!std::filesystem::exists(txt_path, ec))
        print_message_and_die("bittrack.txt does not exist in the directory");
    if (!std::filesystem::exists(bin_path, ec))
        print_message_and_die("bittrack.bin does not exist in the directory");

    // now, open bittrack.txt
    bittrack_kv = parse_kv_file(txt_filepath);

    // get the line and page sizes
    // FUTURE: harmonize line vs. block terminology
    line_size = std::stoull(bittrack_kv["BLOCK_SIZE"]);
    page_size = std::stoull(bittrack_kv["PAGE_SIZE"]);
    // BitTrack ensures these will be powers-of-two
    line_size_log2 = __builtin_ctzll(line_size);
    page_size_log2 = __builtin_ctzll(page_size);
    bits_per_line = line_size * 8;
    bits_per_page = page_size * 8;

    // always load the average from the .txt file:
    average_wf = std::stod(bittrack_kv["P_BITFLIP_PER_WRITE"]);
    average_bfpw = (uint64_t) std::ceil(average_wf * bits_per_line);
    printf("average_wf: %f\n", average_wf);
    printf("average_bfpw: %zu\n", average_bfpw);

    // if in per-page mode, load the bittrack.bin file
    if (write_factor_mode == WF_MODE_PER_PAGE) {
        std::ifstream ifs(bin_filepath, std::ios::binary);

        bittrack_entry_t e;
        while (!ifs.eof()) {
            ifs.read((char*) &e, sizeof(e));
            page_wfs[e.page_addr] = e.page_wf;
            //printf("PWF: %f\n", e.page_wf);
        }

        if (page_wfs.size() != std::stoull(bittrack_kv["N_PAGES_WRITTEN"]))
            print_message_and_die("mismatch in n. pages between .txt and .bin");


        // fill out page_bfpws (page bits flipped per write; i.e., every time we
        // write a line to a page, the count of how many bits expected to flip)
        for (auto& kv : page_wfs) {
            page_addr_t page_addr = kv.first;
            double page_wf = kv.second;

            double page_bfpw_d = page_wf * (double) bits_per_line;
            uint64_t page_bfpw_i = (uint64_t) ceil(page_bfpw_d);

            page_bfpws[page_addr] = page_bfpw_i;
        }

    }
}


void
SNQueues::run()
{
    // first, construct all frames in the initial starting queues state
    do {
        auto& mt = mtr.next();
        auto page_addr = line_addr_to_page_addr(mt.line_addr, line_size_log2,
                page_size_log2);
        if (!page_map.count(page_addr)) {
            // allocate everything in the bottommost queue initially...
            frame_meta_t* fm = new frame_meta_t{0, 0, 0, page_addr};
            queues_vec[0].emplace_back(fm);
            // ...and the page map
            auto lq_back = std::next(queues_vec[0].end(), -1);
            page_map.emplace(page_addr, lq_back);

        }
    }
    while (!mtr.is_end_of_pass());
    mtr.reset(false /* don't increment passes */);


    /*
     * Size the memory. If the number of pages in the trace is higher than what
     * the user requested, set num. pages in mem. to the power of two that
     * is >= rss. If the user requested more pages than what is in the trace,
     * just go with that.
     */
    n_pages_rss = page_map.size();
    n_bytes_rss = n_pages_rss * page_size;

    if (n_pages_rss > n_pages_requested) {
        if (__builtin_popcountll(n_bytes_rss) == 1) {
            // perfect power of two; keep it
            n_bytes_mem = n_bytes_rss;
        }
        else {
            uint64_t n_head_bits = (sizeof(n_bytes_rss) * 8) - 1;
            uint64_t n_bytes_rss_log2_floor =
                    n_head_bits - __builtin_clzll(n_bytes_rss);
            // increment by one; next-highest
            uint64_t n_bytes_mem_log2 = n_bytes_rss_log2_floor + 1;
            n_bytes_mem = ((uint64_t) 1) << n_bytes_mem_log2;
            printf("Requested memory size was < trace RSS; rounding up...\n");
        }
    }
    else n_bytes_mem = n_bytes_requested;
    // set n_pages_mem too
    n_pages_mem = n_bytes_mem / page_size;

    // print some initial stats
    printf("Beginning simulation\n");
    printf("Global MiB in memory: %zu\n", n_bytes_mem / (1024 * 1024));



    // now, prepend the remainder free frames (up to n_pages_mem) to queue 0
    // NOTE: this means multiple frames will represent the 0x0 page addr., but
    // this is fine, as it's just a filler value.
    size_t n_rem_pages = n_pages_mem - n_pages_rss;
    for (size_t i = 0; i < n_rem_pages; ++i) {
        frame_meta_t* fm = new frame_meta_t{0, 0, 0, 0x0};
        queues_vec[0].emplace_front(fm);
    }



    // main loop
    bool cont = true;
    while (cont) {
        if (mtr.is_end_of_pass()) {
            system_time_s += trace_time_s;
            dump_stats(/* final = false; incremental */);

            if (mtr.get_n_full_passes() + 1 == n_iterations) break;
        }

        auto& mt = mtr.next();
        // ignore anything that's not a write
        if (!mt.is_write) continue;

        auto page_addr = line_addr_to_page_addr(mt.line_addr, line_size_log2,
                page_size_log2);

        // get the correct bfpw for the page
        uint64_t page_bfpw = 0;
        if (write_factor_mode == WF_MODE_AVERAGE) {
            page_bfpw = average_bfpw;
        }
        else if (write_factor_mode == WF_MODE_PER_PAGE) {
            auto page_bfpw_it = page_bfpws.find(page_addr);
            page_bfpw = page_bfpw_it == page_bfpws.end() ?
                average_bfpw : page_bfpw_it->second;
        }


        auto fmi = page_map.at(page_addr);
        frame_meta_t* fm = *fmi;
        //printf("FM ptr = %p; fm q=%zu; fm ibf=%zu; fm lbf=%zu\n",
        //        fm, fm->queue, fm->interval_bfs, fm->lifetime_bfs);

        if (fm->interval_bfs >= bucket_interval) {
            //printf("%p hit interval; q=%zu\n", fm, fm->queue);
            // frame has hit its write interval.
            // 1. promote the frame into the next-higher queue
            // 2. in the lowest active queue, "rotate" the head frame with
            //    the tail frame
            // 3. swap the contents of the new tail frame in the lowest
            //    queue with the promoted frame (i.e., update page_addr in
            //    fm and map)
            // NOTE: we do account for extra writes incurred by swap

            size_t old_queue_idx = fm->queue;
            queues_vec[old_queue_idx].erase(fmi);
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
                queues_vec[new_queue_idx].emplace_back(fm);
                fm->queue = new_queue_idx;
                // subtract off the bucket interval to indicate promotion
                fm->interval_bfs -= bucket_interval;
                //printf("q0l: %zu; promotion to %zu; ibfs: %zu\n",
                //        queues_vec[0].size(), fm->queue, fm->interval_bfs);

                // NOTE: we only do the swap to a lower bucket (never to same)
                if (lowest_active_queue < fm->queue) {

                    // pop-and-push in the lowest active queue
                    auto lfm = queues_vec[lowest_active_queue].front();
                    queues_vec[lowest_active_queue].pop_front();
                    queues_vec[lowest_active_queue].emplace_back(lfm);

                    // swap page_addr in l/fm and page_map
                    fm->page_addr = lfm->page_addr;
                    lfm->page_addr = page_addr;

                    // update page_map to reflect the now-swapped mapping. both
                    // frames are now at the back of their respective queues.
                    auto new_queue_back =
                            std::next(queues_vec[new_queue_idx].end(), -1);
                    auto lowest_queue_back =
                            std::next(queues_vec[lowest_active_queue].end(),
                            -1);

                    page_map[fm->page_addr] = new_queue_back;
                    page_map[lfm->page_addr] = lowest_queue_back;


                    // apply the swap write itself to both frames
                    // 1. look up bfpw for the lower frame
                    uint64_t lfm_bfpw = 0;
                    if (write_factor_mode == WF_MODE_AVERAGE) {
                        lfm_bfpw = average_bfpw;
                    }
                    else if (write_factor_mode == WF_MODE_PER_PAGE) {
                        auto lfm_bfpw_it = page_bfpws.find(lfm->page_addr);
                        lfm_bfpw = lfm_bfpw_it == page_bfpws.end() ?
                            average_bfpw : lfm_bfpw_it->second;
                    }
                    // 2. apply to both frames
                    // NOTE: technically, our "bit flip percentages" are defined
                    // only for successive time steps of writes of the same
                    // page onto a frame, and undefined for "page 1" being
                    // remapped onto a frame originally mapped by "page 0".
                    // However, we can approximate the remap bitflip as the
                    // *newly-mapped* page's bitflip value.
                    fm->interval_bfs += lfm_bfpw;
                    fm->lifetime_bfs += lfm_bfpw;
                    lfm->interval_bfs += page_bfpw;
                    lfm->lifetime_bfs += page_bfpw;

                    ++total_n_promotions;

                    // if we're within n_promotions_to_event_trace, trace
                    // the event timestamp (cycle).
                    if (total_n_promotions <= n_promotions_to_event_trace) {
                        uint64_t curr_timestamp = mt.cycle +
                                (mtr.get_n_full_passes() * trace_end_cycle);
                        event_trace.get()->write((char*) &curr_timestamp,
                                sizeof(curr_timestamp));
                    }
                }
            }
        }
        else {
            fm->interval_bfs += page_bfpw;
        }


        //// whether we hit interval or not, increment both bfs
        fm->lifetime_bfs += page_bfpw;


        // always check to update the most-written frame at end
        // nullptr check: ensure we always have some valid most_written_frame
        if (most_written_frame == nullptr or
                fm->lifetime_bfs > most_written_frame->lifetime_bfs) {
            most_written_frame = fm;
        }
    }
}


void
SNQueues::dump_stats(bool final)
{
    /*
     * NOTE: VIAMAX is calculated
     * 1. via the most-written frame, and
     * 2. via the full memory size used in simulation.
     * whereas VIAAVG is calculated
     * 1. via the average of bitflips across the memory, and
     * 2. via the requested memory size.
     */

    // don't want to continuously calculate these, so just do it here
    double most_written_frame_wear_pct =
            (double) most_written_frame->lifetime_bfs / (double) bucket_cap;
    double lifetime_est_viamax_s = (double) system_time_s /
            (double) most_written_frame_wear_pct;
    double lifetime_est_viamax_y = lifetime_est_viamax_s /
            ((double) 86400 * 365);


    // these are only calculated (and printed) upon termination
    double lifetime_est_viaavg_s = 0.0;
    double lifetime_est_viaavg_y = 0.0;
    if (final) {
        // NOTE: calculates the average for num. *requested* bytes
        uint64_t bfs_possible = n_bytes_requested * 8 * cell_write_endurance;
        uint64_t bfs_performed = 0;

        for (auto& l : queues_vec) {
            for (auto& f_it : l) {
                bfs_performed += f_it->lifetime_bfs;
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
        ss << "MEMORY_BYTES_REQUESTED" << " " << n_bytes_requested << std::endl;
        ss << "MEMORY_BYTES_INSIM" << " " << n_bytes_mem << std::endl;
        ss << "MEMORY_PAGES_INSIM" << " " << n_pages_mem << std::endl;
    }

    ss << "FULL_PASSES" << " " << mtr.get_n_full_passes() << std::endl;
    ss << "SYSTEM_TIME_S" << " " << system_time_s << std::endl;
    ss << "MOST_WRITTEN_FRAME_PTR" << " " << most_written_frame << std::endl;
    ss << "MOST_WRITTEN_FRAME_BFS" << " " << most_written_frame->lifetime_bfs
            << std::endl;
    ss << "MOST_WRITTEN_FRAME_WEAR_PCT" << " " << most_written_frame_wear_pct
            << std::endl;
    ss << "MOST_WRITTEN_FRAME_QUEUE" << " " << most_written_frame->queue
            << std::endl;
    ss << "LOWEST_ACTIVE_QUEUE" << " " << lowest_active_queue << std::endl;
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
        std::ofstream ofs("snqueues.txt", std::ofstream::out);
        ofs << ss.rdbuf()->str();
    }
}


int
main(int argc, char* argv[])
{
    SNQueues snq(argc, argv);

    snq.run();
    snq.dump_stats(true /* final = true; termination */);

    return 0;
}
