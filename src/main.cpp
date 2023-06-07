#include <iomanip>
#include <sstream>
#include <vector>
#include <fstream>
#include <iostream>

#include <dpp/dpp.h>
#include <mpg123.h>
#include <out123.h>

#include "token.hpp"

/* For an example we will hardcode a path to some awesome music here */
#define MUSIC_FILE "./phonk.mp3"

int main(int argc, char const *argv[])
{
    mpg123_init();

    std::vector<uint8_t> pcmdata;

    int err = 0;
    unsigned char *buffer;
    size_t buffer_size, done;
    int channels, encoding;
    long rate;

    /* Note it is important to force the frequency to 48000 for Discord compatibility */
    mpg123_handle *mh = mpg123_new(NULL, &err);
    mpg123_param(mh, MPG123_FORCE_RATE, 48000, 48000.0);

    buffer_size = mpg123_outblock(mh);
    buffer = new unsigned char[buffer_size];

    /* Note: In a real world bot, this should have some error logging */
    mpg123_open(mh, MUSIC_FILE);
    mpg123_getformat(mh, &rate, &channels, &encoding);

    unsigned int counter = 0;
    for (int totalBytes = 0; mpg123_read(mh, buffer, buffer_size, &done) == MPG123_OK; ) {
        for (auto i = 0; i < buffer_size; i++) {
            pcmdata.push_back(buffer[i]);
        }
        counter += buffer_size;
        totalBytes += done;
    }
    delete buffer;
    mpg123_close(mh);
    mpg123_delete(mh);

    dpp::cluster bot(BOT_TOKEN, dpp::i_default_intents | dpp::i_message_content);

    bot.on_log(dpp::utility::cout_logger());

    bot.on_message_create([&bot, &pcmdata](dpp::message_create_t const &event) {
        std::stringstream ss(event.msg.content);
        std::string command;
        ss >> command;

        /* Tell the bot to join the discord voice channel the user is on. Syntax: .join */
        if (command == ".join") {
            dpp::guild *g = dpp::find_guild(event.msg.guild_id);
            if (!g->connect_member_voice(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, "You don't seem to be on a voice channel! :("));
            }
        }

        if (command == ".mp3") {
            dpp::voiceconn* v = event.from->get_voice(event.msg.guild_id);
            if (v && v->voiceclient && v->voiceclient->is_ready()) {
                /* Stream the already decoded MP3 file. This passes the PCM data to the library to be encoded to OPUS */
                v->voiceclient->send_audio_raw((uint16_t*)pcmdata.data(), pcmdata.size());
            }
        }
    });

    bot.start(dpp::st_wait);

    mpg123_exit();

    return 0;
}
