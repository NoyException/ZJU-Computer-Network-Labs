#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    // Your code here.
    _routes.insert({route_prefix, prefix_length, next_hop, interface_num});
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    const Route* route = nullptr;
    // 1.路由器在路由表中查找数据报目的地址匹配的路由，即目的地址的最长prefix_length与route_prefix的最长prefix_length相同。
    // 2.在匹配的路由中，路由器选择prefix_length最长的路由。
    for (auto &item : _routes){
        //这里如果prefix_length为0，那么左移时会出现bug（左移size超过类型大小），所以需要特殊处理
        uint32_t ip = item.prefix_length == 0 ? 0 : (dgram.header().dst>>(32-item.prefix_length))<<(32-item.prefix_length);
        if(ip==item.route_prefix){
            route = &item;
            break;
        }
    }
    // 3.如果没有匹配的路由，则丢弃该数据报。
    if(route == nullptr){
        return;
    }
    // 4.路由器减少数据报的TTL（存活时间）。如果TTL已经为零，或者在减少之后达到零，路由器应该丢弃数据报。
    if(dgram.header().ttl <= 1){
        dgram.header().ttl = 0;
        return;
    }
    dgram.header().ttl--;
    // 5.否则，路由器将修改后的数据报从接口发送到适当的下一跳（interface(interface_num).send_datagram()）。
    if(route->next_hop.has_value()){
        interface(route->interface_num).send_datagram(dgram, route->next_hop.value());
    }
    else{
        interface(route->interface_num).send_datagram(dgram, Address::from_ipv4_numeric(dgram.header().dst));
    }
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
