#pragma once

#include <TiltedCore/Stl.hpp>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <mutex>

struct ServerLogSink : spdlog::sinks::base_sink<std::mutex>
{
    explicit ServerLogSink(size_t aMaxLines);

    void ConsumeLines(TiltedPhoques::Vector<TiltedPhoques::String>& aOut);
    void PushExternalLine(const char* aLine);

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override;
    void flush_() override {}

private:
    size_t m_maxLines;
    TiltedPhoques::Vector<TiltedPhoques::String> m_lines;
    size_t m_readIndex{0};
};

std::shared_ptr<ServerLogSink> GetServerLogSink();
void AttachServerLogSinkToLogger(const std::shared_ptr<spdlog::logger>& aLogger);
void AttachServerLogSinkToAllLoggers();
