#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    auto it = this->_map.find(next_hop_ip);
    //1.如果目标以太网地址已经知道，立即发送
    if(it != this->_map.end()){
        //创建一个类型为EthernetHeader::TYPE_IPv4的以太网帧
        EthernetFrame frame;
        frame.header().type = EthernetHeader::TYPE_IPv4;
        //将序列化的数据报设置为负载，并设置源地址和目的地址
        frame.payload() = dgram.serialize();
        frame.header().src = _ethernet_address;
        frame.header().dst = it->second.first;
        _frames_out.push(frame);
    }
    //2.如果目的以太网地址未知
    else {
        auto it2 = _arp_timestamp.find(next_hop_ip);
        if(it2 == _arp_timestamp.end() || _time - it2->second > ARP_TTL) {
            //则广播请求下一跳的以太网地址
            ARPMessage arp_request;
            arp_request.opcode = ARPMessage::OPCODE_REQUEST;
            arp_request.sender_ethernet_address = _ethernet_address;
            arp_request.sender_ip_address = _ip_address.ipv4_numeric();
            arp_request.target_ethernet_address = {0};
            arp_request.target_ip_address = next_hop_ip;

            EthernetFrame frame_request;
            frame_request.header().dst = ETHERNET_BROADCAST;
            frame_request.header().src = _ethernet_address;
            frame_request.header().type = EthernetHeader::TYPE_ARP;
            frame_request.payload() = arp_request.serialize();
            _frames_out.push(frame_request);

            _arp_timestamp[next_hop_ip] = _time;
        }
        //并将IP数据报排队，以便在收到ARP应答后发送
        this->_arp_cache.insert({next_hop_ip, dgram});
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    //当以太网帧从网络到达时调用此方法，代码应该忽略任何不发送到该网络接口的帧。

    //检查帧的目的地址是否是本机
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST) {
        return nullopt;
    }

    switch (frame.header().type) {
        //1.如果入站帧是IPv4
        case EthernetHeader::TYPE_IPv4 : {
            //将有效负载解析为InternetDatagram
            InternetDatagram datagram;
            auto result = datagram.parse(frame.payload());
            //如果成功（意味着parse()方法返回ParseResult::NoError）
            if (result == ParseResult::NoError) {
                //将结果InternetDatagram返回给调用者
                return datagram;
            }
        }
        break;
        //2.如果入站帧是ARP，将负载解析为ARPMessage
        case EthernetHeader::TYPE_ARP : {
            ARPMessage arp_message;
            auto result = arp_message.parse(frame.payload());
            //如果成功
            if(result == ParseResult::NoError) {
                //记住发送者的IP地址和以太网地址之间的映射关系30秒
                if(arp_message.target_ip_address == _ip_address.ipv4_numeric()) {
                    this->_map[arp_message.sender_ip_address] = {arp_message.sender_ethernet_address, this->_time};
                }

                //另外，如果它是一个请求我们的IP地址的ARP请求，发送一个适当的ARP回复
                if(arp_message.opcode == ARPMessage::OPCODE_REQUEST) {
                    if(arp_message.target_ip_address == _ip_address.ipv4_numeric()) {
                        ARPMessage arp_reply;
                        arp_reply.hardware_type = arp_message.hardware_type;
                        arp_reply.protocol_type = arp_message.protocol_type;
                        arp_reply.hardware_address_size = arp_message.hardware_address_size;
                        arp_reply.protocol_address_size = arp_message.protocol_address_size;
                        arp_reply.opcode = ARPMessage::OPCODE_REPLY;
                        arp_reply.sender_ethernet_address = _ethernet_address;
                        arp_reply.sender_ip_address = _ip_address.ipv4_numeric();
                        arp_reply.target_ethernet_address = arp_message.sender_ethernet_address;
                        arp_reply.target_ip_address = arp_message.sender_ip_address;
                        EthernetFrame frame_reply;
                        frame_reply.header().dst = arp_message.sender_ethernet_address;
                        frame_reply.header().src = _ethernet_address;
                        frame_reply.header().type = EthernetHeader::TYPE_ARP;
                        frame_reply.payload() = arp_reply.serialize();
                        _frames_out.push(frame_reply);
                    }
                }
                else if(arp_message.opcode == ARPMessage::OPCODE_REPLY) {
                    //发送排队的IP数据报
                    auto range = this->_arp_cache.equal_range(arp_message.sender_ip_address);
                    for(auto it = range.first; it != range.second; it++) {
                        EthernetFrame frame_reply;
                        frame_reply.header().dst = arp_message.sender_ethernet_address;
                        frame_reply.header().src = _ethernet_address;
                        frame_reply.header().type = EthernetHeader::TYPE_IPv4;
                        frame_reply.payload() = it->second.serialize();
                        _frames_out.push(frame_reply);
                    }
                    this->_arp_cache.erase(arp_message.sender_ip_address);
                }
            }
        }
    }
    return nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    _time += ms_since_last_tick;
    //去除超时的映射
    for(auto it = this->_map.begin(); it != this->_map.end();) {
        if(this->_time - it->second.second > TTL) {
            it = this->_map.erase(it);
        }
        else {
            it++;
        }
    }
}
