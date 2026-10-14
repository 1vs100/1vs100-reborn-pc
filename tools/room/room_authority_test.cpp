#include "room_authority.h"
#include <iostream>
#include <stdexcept>
void check(bool value) { if (!value) throw std::runtime_error("authority invariant failed"); }
int main() {
  try {
    RoomAuthority room(11);
    check(!room.Invite(22,22)); check(room.Invite(11,22));
    check(!room.Publish(22,1,"forged"));
    check(room.Publish(11,1,"original GameState payload"));
    check(!room.Publish(11,1,"stale")); check(!room.Publish(11,3,"gap"));
    uint64_t revision = 0; std::string state;
    check(!room.Read(33,revision,state)); check(room.Read(22,revision,state));
    check(revision == 1 && state == "original GameState payload");
    check(!room.Contribute(33,1,"TotalCorrect",1));
    check(room.Contribute(22,1,"TotalCorrect",1));
    check(room.Contribute(22,1,"TotalCorrect",1)); check(room.Sum("TotalCorrect") == 1);
    check(!room.Contribute(22,1,"TotalCorrect",9));
    check(room.Contribute(11,1,"TotalCorrect",1)); check(room.Sum("TotalCorrect") == 2);
    check(room.Contribute(11,2,"Overflow",INT64_MAX));
    check(!room.Contribute(11,3,"Overflow",1));
    check(room.Publish(11,2,"next question")); check(room.Read(22,revision,state));
    check(revision == 2 && state == "next question");
    std::cout << "PASS host-only state, membership, revision, retry, sum and overflow invariants\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
