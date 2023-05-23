#include "catch_amalgamated.hpp"
#include "../src/track.h"

TEST_CASE("Add clip")
{
    daw::AudioTrack track;

    track.add_node(new daw::ClipNode{
                        .start_time = 10,
                        .end_time = 13
                   });

    track.add_node(new daw::ClipNode{
                        .start_time = 0,
                        .end_time = 9
                   });
    
    track.add_node(new daw::ClipNode{
                        .start_time = 21,
                        .end_time = 25
                   });

    track.add_node(new daw::ClipNode{
                        .start_time = 14,
                        .end_time = 16
                   });

    track.add_node(new daw::ClipNode{
                        .start_time = 26,
                        .end_time = 30
                   });

    REQUIRE(true);
}

TEST_CASE("Find clip node at given time")
{
    daw::AudioTrack track;

    track.add_node(new daw::ClipNode{
                        .start_time = 10,
                        .end_time = 13
                   });

    track.add_node(new daw::ClipNode{
                        .start_time = 3,
                        .end_time = 9
                   });

    track.add_node(new daw::ClipNode{
                        .start_time = 21,
                        .end_time = 25
                   });

    track.add_node(new daw::ClipNode{
                        .start_time = 14,
                        .end_time = 16
                   });

    track.add_node(new daw::ClipNode{
                        .start_time = 26,
                        .end_time = 30
                   });

    daw::ClipNode* node = track.node_at(21);
    REQUIRE((node->start_time == 21 && node->end_time == 25));
    REQUIRE(track.node_at(2) == nullptr);
}
