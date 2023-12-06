#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _retransmission_timeout{retx_timeout}
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return _next_seqno - _acked_seqno; }

void TCPSender::fill_window() {
    if ((_window_size == 0 ? 1 : _window_size) - bytes_in_flight() <= 0)
        return;

    // 首次发送数据时，发送一个SYN
    if (_next_seqno == 0) {
        TCPSegment segment;
        segment.header().syn = true;
        segment.header().seqno = next_seqno();
        send_segment(segment);
        _next_seqno++;
        return;
    }

    // stream eof时，发送一个FIN
    if (_stream.eof() && _next_seqno < _stream.bytes_written() + 2) {
        if (_fin)
            return;
        _fin = true;
        TCPSegment segment;
        segment.header().fin = true;
        segment.header().seqno = next_seqno();
        send_segment(segment);
        _next_seqno++;
        return;
    }

    // 发送数据
    while (_stream.buffer_size() > 0 && _next_seqno < _stream.bytes_written() + 2) {
        TCPSegment segment;
        segment.header().seqno = next_seqno();
        size_t len = min(TCPConfig::MAX_PAYLOAD_SIZE, _stream.buffer_size());
        size_t maxLen = (_window_size == 0 ? 1 : _window_size) - bytes_in_flight();
        len = min(len, maxLen);

        if (len <= 0)
            break;

        segment.payload() = _stream.read(len);
        // 如果发送的数据长度小于最大负载长度（留一个位给FIN），并且输入流已关闭，那么就设置FIN标志
        if (len < maxLen && _stream.eof()) {
            segment.header().fin = true;
            _fin = true;
            _next_seqno++;
        }
        send_segment(segment);
        _next_seqno += len;

        if (len == maxLen)
            break;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t ackno_absolute = unwrap(ackno, _isn, _next_seqno);

    if (ackno_absolute > _next_seqno)
        return;

    // 如果对端的ackno等于已经确认的最大序列号，那么就只更新窗口大小，不做其他操作
    if (ackno_absolute == _acked_seqno)
        _window_size = max(_window_size, window_size);

    // 只有当对端的ackno大于已经确认的最大序列号时，才会更新已经确认的最大序列号，并更新其它状态
//    if (ackno_absolute <= _acked_seqno)
//        return;

    bool updated = false;
    while (!_segments_not_acked.empty()) {
        auto segment = _segments_not_acked.front();
        _acked_seqno = unwrap(segment.header().seqno, _isn, _acked_seqno);
        if (_acked_seqno+segment.length_in_sequence_space() > ackno_absolute)
            break;
        _segments_not_acked.pop();
        updated = true;
    }

    if(!updated)
        return;

    _acked_seqno = ackno_absolute;

    _timestamp = _time;
    _consecutive_retransmissions = 0;
    _retransmission_timeout = _initial_retransmission_timeout;
    _window_size = window_size;
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    _time += ms_since_last_tick;
    if (_segments_not_acked.empty())
        _timestamp = _time;
    else {
        if (_time - _timestamp >= _retransmission_timeout) {
            _segments_out.push(_segments_not_acked.front());
            _timestamp = _time;
            // 如果窗口大小为0，那么就会一直以稳定的速率重传1个字节的数据，以探测对端是否已经恢复
            if(_window_size!=0){
                _consecutive_retransmissions++;
                _retransmission_timeout *= 2;
            }
        }
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment segment;
    segment.header().seqno = next_seqno();
    segment.header().ack = true;
    _segments_out.push(segment);
}

void TCPSender::send_segment(const TCPSegment &segment) {
    _segments_out.push(segment);
    _segments_not_acked.push(segment);
}