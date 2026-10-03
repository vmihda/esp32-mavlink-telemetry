#pragma once

// Starts Wi-Fi (access point, plus station mode when credentials are configured)
// and the HTTP server.
void webBegin();

// Services HTTP clients; call from loop().
void webLoop();
