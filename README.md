[![Notion](https://img.shields.io/badge/노션_포트폴리오_바로가기-%232F343B.svg?style=for-the-badge&logo=notion&logoColor=white)](https://app.notion.com/p/3198942a9aa6809a8d64fe9d64a47f67?source=copy_link)

이거 다크그레이로 ㅇㅋ?

<p align="center">
  <img width="800" alt="게임 가이드" src="https://github.com/user-attachments/assets/f917a9ce-16a1-4780-ad23-fd436aa8a836" />
  <br>
  <sub>▲ 게임 가이드</sub>
</p>

<br>

<p align="center">
  <img width="250" alt="인게임 UI" src="https://github.com/user-attachments/assets/1ac5be94-9fe3-471f-b9c8-6a7ed3f8add0" />
  <br>
  <sub>▲ 인게임 UI</sub>
</p>

---

# 🎮 게임 시스템 및 룰

하나의 키보드로 두 명의 플레이어가 동시에 대결합니다. 

내 손패의 합을 높이고 상대를 방해하는 3가지 조작을 실시간으로 수행해야 합니다.
정해진 제한 시간이 모두 끝나는 그 순간, 자신이 보유한 '카드 숫자의 총합'이 상대방보다 높은 플레이어가 최종 승리합니다.

* 가져오기 (TAKE): 중앙 카드 더미에서 카드를 가져와 내 점수를 높입니다. (P1: `A` / P2: `J`)
* 버리기 (PUT): 내 손에 있는 불필요한 카드를 다시 중앙 더미로 던져버립니다. (P1: `S` / P2: `K`)
* 넘기기 (GIVE): 내 카드를 상대방에게 강제로 쥐여줍니다. 상대의 치밀한 전략을 망가뜨릴 수 있는 핵심 심리전 기술입니다. (P1: `D` / P2: `L`)

---

### 🛠️ 실행 방법

리눅스(Ubuntu) 환경의 터미널에서 아래 명령어로 컴파일 및 실행합니다.

```bash
gcc -o Card Card.c -pthread
./Card
