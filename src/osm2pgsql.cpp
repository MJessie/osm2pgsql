/*
#-----------------------------------------------------------------------------
# osm2pgsql - converts planet.osm file into PostgreSQL
# compatible output suitable to be rendered by mapnik
# Use: osm2pgsql planet.osm.bz2
#-----------------------------------------------------------------------------
# Original Python implementation by Artem Pavlenko
# Re-implementation by Jon Burgess, Copyright 2006
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#-----------------------------------------------------------------------------
*/

#include "db-copy.hpp"
#include "format.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "options.hpp"
#include "osmdata.hpp"
#include "osmtypes.hpp"
#include "output.hpp"
#include "parse-osmium.hpp"
#include "reprojection.hpp"
#include "util.hpp"
#include "version.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include <libpq-fe.h>

int main(int argc, char *argv[])
{
    fmt::print(stderr, "osm2pgsql version {}\n\n", get_osm2pgsql_version());

    try {
        //parse the args into the different options members
        options_t const options = options_t(argc, argv);
        if (options.long_usage_bool) {
            return 0;
        }

        //setup the middle and backend (output)
        std::shared_ptr<middle_t> middle;

        if (options.slim) {
            // middle gets its own copy-in thread
            middle = std::shared_ptr<middle_t>(new middle_pgsql_t{&options});
        } else {
            middle = std::shared_ptr<middle_t>(new middle_ram_t{&options});
        }

        middle->start();

        auto const outputs =
            output_t::create_outputs(middle->get_query_instance(), options);
        //let osmdata orchestrate between the middle and the outs
        osmdata_t osmdata{middle, outputs};

        fmt::print(stderr, "Using projection SRS {} ({})\n",
                   options.projection->target_srs(),
                   options.projection->target_desc());

        //start it up
        util::timer_t timer_overall;
        osmdata.start();

        /* Processing
         * In this phase the input file(s) are read and parsed, populating some of the
         * tables. Not all ways can be handled before relations are processed, so they're
         * set as pending, to be handled in the next stage.
         */
        parse_stats_t stats;
        //read in the input files one by one
        for (auto const &filename : options.input_files) {
            //read the actual input
            fmt::print(stderr, "\nReading in file: {}\n", filename);
            util::timer_t timer_parse;

            parse_osmium_t parser(options.bbox, options.append, &osmdata);
            parser.stream_file(filename, options.input_reader);

            stats.update(parser.stats());

            fmt::print(stderr, "  parse time: {}s\n", timer_parse.stop());
        }

        //show stats
        stats.print_summary();

        //Process pending ways, relations, cluster, and create indexes
        osmdata.stop();

        fmt::print(stderr, "\nOsm2pgsql took {}s overall\n",
                   timer_overall.stop());

        return 0;
    } catch (std::runtime_error const &e) {
        fmt::print(stderr, "Osm2pgsql failed due to ERROR: {}\n", e.what());
        exit(EXIT_FAILURE);
    }
}
