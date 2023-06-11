#include <vector>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <cassert>
#include <memory>
#include <regex>
#include <source_location>

#include <dpp/dpp.h>
#include <oggz/oggz.h>
#include <mpg123.h>
#include <out123.h>

#include "on_scope_exit.hpp"
#include "token.hpp"

static std::vector<uint8_t> s_audio_buffer{};
// static std::string s_current_track{};

std::string download_song(
    std::string const &url,
    std::string const &format
) {
    s_audio_buffer.clear();

    std::string_view platform, unique_id;
    {
        static std::regex const YOUTUBE_FULL("(?:https:\\/\\/)?www\\.youtube\\.com\\/watch\?v=([a-zA-Z0-9]+)");
        static std::regex const YOUTUBE_MANGLED("(?:https:\\/\\/)?youtu\\.be\\/([-_a-zA-Z0-9]+)");

        if (std::regex_match(url, YOUTUBE_FULL)) {
            platform = "yt_full";
            size_t one_before_id_start_pos = url.find_last_of('=');
            assert(one_before_id_start_pos != std::string::npos);
            unique_id = std::string_view(url.c_str() + one_before_id_start_pos + 1);
        }
        else if (std::regex_match(url, YOUTUBE_MANGLED)) {
            platform = "yt_mangled";
            size_t one_before_id_start_pos = url.find_last_of('/');
            assert(one_before_id_start_pos != std::string::npos);
            unique_id = std::string_view(url.c_str() + one_before_id_start_pos + 1);
        }
        else {
            throw std::runtime_error("unsupported platform");
        }
    }

    std::string save_path; // "data/platform.unique_id.format"
    save_path.reserve(strlen("data/") + platform.length() + 1 + unique_id.length() + 1 + format.length());
    save_path.append("data/").append(platform).append(".").append(unique_id).append(".").append(format);

    std::stringstream command{};
    command
        << "yt-dlp "
        << " -x"
        << " --audio-format " << format
        << " --audio-quality 0"
        << " -o " << save_path
        << " --no-playlist"
        << " -- " << url;

    FILE *pipe = popen(command.str().c_str(), "r");

    if (!pipe) {
        throw std::runtime_error("failed to open yt-dlp pipe");
    }

    // Read the output of yt-dlp
    {
        char buffer[16'384];
        std::string result;
        result.reserve(sizeof(buffer));

        while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            result += buffer;
        }
    }

    pclose(pipe);

    // load song data into s_audio_buffer
    if (format == "mp3") {
        int err = 0;
        mpg123_handle *mh = nullptr;

        auto const throw_on_mpg123_error = [mh, &err](std::source_location loc = std::source_location::current()) -> bool {
            if (err != MPG123_OK) {
                char const *err_str = mpg123_strerror(mh);
                std::stringstream err{};
                err << loc.file_name() << ':' << loc.line() << ' ' << err_str;
                throw std::runtime_error(err.str());
            }
            return true;
        };

        mh = mpg123_new(nullptr, &err);
        throw_on_mpg123_error();

        mpg123_param(mh, MPG123_FORCE_RATE, 48000, 48000.0);

        size_t const buffer_size = mpg123_outblock(mh);
        auto buffer = std::make_unique<uint8_t []>(buffer_size);

        err = mpg123_open(mh, save_path.c_str());
        throw_on_mpg123_error();

        auto cleanup_routine = make_on_scope_exit([&]() {
            (void)mpg123_close(mh);
            mpg123_delete(mh);
        });

        long rate;
        int channels, encoding;
        err = mpg123_getformat(mh, &rate, &channels, &encoding);
        throw_on_mpg123_error();

        for (
            size_t counter = 0, total_bytes = 0, done = 0;
            mpg123_read(mh, buffer.get(), buffer_size, &done) == MPG123_OK;
        ) {
            std::copy(buffer.get(), buffer.get() + buffer_size, std::back_inserter(s_audio_buffer));
            counter += buffer_size;
            total_bytes += done;
        }
    }
    else if (format == "opus" || format == "") {
        std::ifstream opus_file(save_path, std::ios::binary);

        if (!opus_file) {
            std::stringstream err{};
            err << "failed to open '" << save_path << "'";
            throw std::runtime_error(err.str());
        }

        auto const file_size = std::filesystem::file_size(save_path);
        assert(file_size <= std::numeric_limits<std::streamsize>::max());

        s_audio_buffer.resize(file_size);

        opus_file.read((char *)s_audio_buffer.data(), file_size);
    }

    return save_path;
}

int main()
try {
    {
        int err = mpg123_init();
        if (err) {
            std::cerr << "mpg123_init failed\n";
            std::exit(1);
        }
    }

    auto main_cleanup_routine = make_on_scope_exit([]() {
        mpg123_exit();
    });

    s_audio_buffer.reserve(1024 * 1024 * 5);

    dpp::cluster bot(BOT_TOKEN, dpp::i_default_intents | dpp::i_message_content);

    bot.on_log(dpp::utility::cout_logger());

    bot.on_voice_track_marker([&bot](dpp::voice_track_marker_t const &event) {
        std::cout << "Tracks remaining: " << event.voice_client->get_tracks_remaining() << '\n';
    });

    bot.on_message_create([&bot](dpp::message_create_t const &event) {
        std::stringstream ss(event.msg.content);
        std::string command;
        ss >> command;

        if (command == ".join") {
            dpp::guild *guild = dpp::find_guild(event.msg.guild_id);

            bool requestor_in_vc = guild->connect_member_voice(event.msg.author.id);

            if (!requestor_in_vc) {
                bot.message_create(dpp::message(event.msg.channel_id, "You don't seem to be on a voice channel! :("));
            }
        }
        else if (command == ".skip") {
            dpp::voiceconn *vconn = event.from->get_voice(event.msg.guild_id);

            if (vconn && vconn->voiceclient) {
                vconn->voiceclient->skip_to_next_marker();
            }
        }
        else if (command == ".play") {
            std::string url = "", format = "";
            ss >> url >> format;

            if (url.empty()) {
                std::cout << "No URL specified\n";
                return;
            }

            if (format.empty()) {
                format = "opus";
            }

            std::cout << "Attempting to play " << url << " as " << format << '\n';

            std::string save_path;
            try {
                save_path = download_song(url, format);
            }
            catch (std::exception const &except) {
                std::cerr << "download_song failed: " << except.what() << '\n';
                return;
            }
            catch (...) {
                std::cerr << "download_song failed: unknown error, catch(...)\n";
                return;
            }

            dpp::voiceconn *vconn = event.from->get_voice(event.msg.guild_id);

            auto const ready_for_send = [vconn]() -> bool {
                return vconn && vconn->voiceclient && vconn->voiceclient->is_ready();
            };

            // if voice connection not ready yet, give it some time to become ready before aborting
            {
                size_t sleep_total_sec = 0;
                size_t const MAX_ALLOWED_SLEEP_SEC = 3;

                while (!ready_for_send() && sleep_total_sec < MAX_ALLOWED_SLEEP_SEC) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    sleep_total_sec += 1;
                }

                if (sleep_total_sec == MAX_ALLOWED_SLEEP_SEC) {
                    std::cout << "Voice NOT ready after waiting " << sleep_total_sec << " seconds, aborting\n";
                    return;
                }

                std::cout << "Voice ready after waiting " << sleep_total_sec << " seconds\n";
            }

            // try to send audio data
            if (format == "mp3") {
                size_t bytes_processed = 0, total_chunks = 0, num_chunks_failed = 0;

                while (bytes_processed <= s_audio_buffer.size()) {
                    size_t const IDEAL_CHUNK_SIZE = 11'520;
                    size_t bytes_remaining = s_audio_buffer.size() - bytes_processed;

                    size_t chunk_size = std::min(IDEAL_CHUNK_SIZE, bytes_remaining);

                    if (ready_for_send()) {
                        vconn->voiceclient->send_audio_raw((uint16_t *)(s_audio_buffer.data() + bytes_processed), chunk_size);
                    } else {
                        ++num_chunks_failed;
                    }

                    bytes_processed += chunk_size;
                    ++total_chunks;
                }
            }
            else if (format == "opus" || format == "") {
                OGGZ *opus_track = oggz_open(save_path.c_str(), OGGZ_READ);

                if (opus_track == nullptr) {
                    std::stringstream err{};
                    err << "failed to open '" << save_path << "'";
                    throw std::runtime_error(err.str());
                }

                size_t num_packets_sent = 0;
                size_t num_packets_total = 0;

                void *callback_user_data[] {
                    (void *)vconn,
                    (void *)&num_packets_sent,
                    (void *)&num_packets_total,
                };

                oggz_set_read_callback(
                    opus_track,
                    -1,
                    [](OGGZ *, oggz_packet *packet, long, void *user_data) {
                        typedef uint64_t any_pointer_t;
                        auto user_data_typed = (any_pointer_t *)user_data;

                        dpp::voiceconn *vc = (dpp::voiceconn *)(user_data_typed[0]);
                        size_t *num_sent = (size_t *)user_data_typed[1];
                        size_t *num_total = (size_t *)user_data_typed[2];

                        if (vc) {
                            vc->voiceclient->send_audio_opus(packet->op.packet, packet->op.bytes);
                            ++(*num_sent);
                        }
                        ++(*num_total);

                        return 0;
                    },
                    callback_user_data
                );

                size_t num_chunks = 0;

                while (ready_for_send()) {
                    long const CHUNK_SIZE = 128;
                    long read_bytes = oggz_read(opus_track, CHUNK_SIZE);

                    if (read_bytes == 0) {
                        break; // EOF
                    }

                    ++num_chunks;
                }

                std::cout << "Sent " << num_packets_sent << '/' << num_packets_total << " packets (" << num_chunks << " chunks)\n";
            }

            vconn->voiceclient->insert_marker(save_path);
            std::cout << "Inserted marker " << save_path << '\n';
            // s_current_track = std::move(save_path);
        }
    });

    bot.start(dpp::st_wait);

    return 0;
}
catch (std::exception const &except) {
    std::cerr << "error: " << except.what() << '\n';
    return 1;
}
catch (...) {
    std::cerr << "error: catch(...)\n";
    return 1;
}
