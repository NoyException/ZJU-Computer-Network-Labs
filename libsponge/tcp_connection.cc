#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const {
    return _sender.stream_in().remaining_capacity();
}

size_t TCPConnection::bytes_in_flight() const {
    return _sender.bytes_in_flight();
}

size_t TCPConnection::unassembled_bytes() const {
    return _receiver.unassembled_bytes();
}

size_t TCPConnection::time_since_last_segment_received() const {
    return _sender.get_time()-_timestamp;
}

void TCPConnection::segment_received(const TCPSegment &seg) {

    if(!_is_active)
        return;

    _timestamp = _sender.get_time();

    // 如果设置了RST标志，将入站流和出站流都设置为错误状态，并永久终止连接
    if(seg.header().rst){
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _is_active = false;
        return;
    }
    // 把这个段交给TCPReceiver
    _receiver.segment_received(seg);
    // 如果设置了ACK标志，则告诉TCPSender它关心的传入段的字段：ackno和window_size。
    if(seg.header().ack){
        _sender.ack_received(seg.header().ackno, seg.header().win);
    }
    
    //状态变化(按照个人的情况可进行修改)
    // 如果是 LISTEN 到了 SYN
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::SYN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED) {
        // 此时肯定是第一次调用 fill_window，因此会发送 SYN + ACK
        connect();
        return;
    }

    // 判断 TCP 断开连接时是否时需要等待
    // CLOSE_WAIT
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED)
        _linger_after_streams_finish = false;

    // 如果到了准备断开连接的时候。服务器端先断
    // CLOSED
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED && !_linger_after_streams_finish) {
        _is_active = false;
        return;
    }

    // 如果收到的数据包里没有任何数据，则这个数据包可能只是为了 keep-alive
    if (seg.length_in_sequence_space() || (_receiver.ackno().has_value() && seg.header().seqno == _receiver.ackno().value() - 1))
        _sender.send_empty_segment();

    load_segments_out();
}

bool TCPConnection::active() const {
    return _is_active;
}

size_t TCPConnection::write(const string &data) {
    size_t t = _sender.stream_in().write(data);
    load_segments_out();
    return t;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if(!_is_active)
        return;
    _sender.tick(ms_since_last_tick);
    // 2.如果连续重传的次数超过上限TCPConfig::MAX_RETX_ATTEMPTS，则终止连接，并发送一个重置段给对端（设置了RST标志的空段）。
    if(_sender.consecutive_retransmissions()>TCPConfig::MAX_RETX_ATTEMPTS){
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _is_active = false;

        TCPSegment segment;
        segment.header().rst = true;
        _segments_out.push(segment);
    }
    load_segments_out();
    if(_receiver.stream_out().input_ended() && _sender.bytes_in_flight()==0 && _sender.stream_in().eof()
        && (!_linger_after_streams_finish || time_since_last_segment_received()>=10*_cfg.rt_timeout)){
        _is_active = false;
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    load_segments_out();
}

void TCPConnection::connect() {
    load_segments_out(true);
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            TCPSegment segment;
            segment.header().rst = true;
            _segments_out.push(segment);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::load_segments_out(bool connect) {
    if(!_is_active)
        return;
    // 调用connect()前，next_seqno_absolute()为0，此时不会发送SYN段
    if(_sender.next_seqno_absolute()==0 && !connect)
        return;
    // 1.TCPSender将一个段推入它的传出队列，并设置了它在传出段上负责的字段（segno，SYN,负载以及FIN）。
    _sender.fill_window();
    while(!_sender.segments_out().empty()){
        auto segment = _sender.segments_out().front();
        _sender.segments_out().pop();
        // 2.在发送段之前，TCPConnection将向TCPReceiver询问它负责传出段的字段：ackno和window_size，如果有一个ackno，它将设置ACK标志以及TCPSegment中的内容。
        if(_receiver.ackno().has_value()){
            segment.header().ack = true;
            segment.header().ackno = _receiver.ackno().value();
            segment.header().win = _receiver.window_size();
        }
        _segments_out.push(segment);
    }
}
