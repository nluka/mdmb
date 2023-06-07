#include <dpp/dpp.h>

const std::string BOT_TOKEN = "MTAzNzkyNDU3NTU2MTkxNjQ2Ng.G_hsLS.B6iBDkHWLxrmR-EFmqYrHCFqLnj-osIm8xyw2E";

int main() {
    dpp::cluster bot(BOT_TOKEN);

    bot.on_log(dpp::utility::cout_logger());

    bot.on_slashcommand([](const dpp::slashcommand_t& event) {
         if (event.command.get_command_name() == "ping") {
            event.reply("Pong!");
        }
    });

    bot.on_ready([&bot]([[maybe_unused]] const dpp::ready_t& event) {
        if (dpp::run_once<struct register_bot_commands>()) {
            bot.global_command_create(
                dpp::slashcommand("ping", "Ping pong!", bot.me.id)
            );
        }
    });

    bot.start(dpp::st_wait);
}
