#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    // 每次从对端接收到新的段时，都会调用该方法。
    // 如果需要，设置初始序列号。设置了SYN标志的第一个到达的网段的序列号设置为ISN。
    // 推送任何数据到StreamReassembler，包括流结束标记。如果FIN标志设置在TCPSegment的报头中，这意味着有效负载的最后一个字节是整个流的最后一个字节。
    auto header = seg.header();

    WrappingInt32 seqno = header.seqno;

    if(header.syn){
        if(_isn.has_value())
            return;
        _isn = seqno;
        seqno = seqno + 1;
    }

    if(!_isn.has_value())
        return;

    if(header.fin){
        if(_fin)
            return;
        _fin = true;
    }

    // -1是因为要去除SYN
    size_t index = unwrap(seqno, _isn.value(), _reassembler.stream_out().bytes_written())-1;

    _reassembler.push_substring(seg.payload().copy(), index, header.fin);

}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if(!_isn.has_value())
        return nullopt;
    auto out = _reassembler.stream_out();
    return wrap(out.bytes_written() + (out.input_ended()?2:1), _isn.value());
}

size_t TCPReceiver::window_size() const {
    return _capacity - _reassembler.stream_out().buffer_size();
}
