| 방향             | type      | 설명                          | Payload                                                                                  |
| -------------- | --------- | --------------------------- | ---------------------------------------------------------------------------------------- |
| **클라이언트 → 서버** | `auth`    | 세션 인증 요청                    | —                                                                                        |
|                | `join`    | 방 입장 요청                     | `{ room: number }`                                                                       |
|                | `leave`   | 방 나가기 요청                    | — *(서버는 내부 `cli->room_id` 사용)*                                                           |
|                | `message` | 채팅 메시지 전송                   | `{ room: number, content: string }`                                                      |
|                | `pong`    | 서버 `ping` 에 대한 응답           | —                                                                                        |
| **서버 → 클라이언트** | `auth_ok` | 인증 성공 응답                    | —                                                                                        |
|                | `joined`  | 누군가 방에 입장했음을 브로드캐스트         | `{ room: number, users: [user_id, …] }`                                                  |
|                | `left`    | 누군가 방을 나갔음을 브로드캐스트          | `{ room: number, user: user_id }`                                                        |
|                | `message` | 새 채팅 메시지 브로드캐스트             | `{ room: number, id: number, sender: user_id, nick: string, content: string, ts: unix }` |
|                | `ping`    | 애플리케이션 레벨 heartbeat (서버→클라) | —                                                                                        |
|                | `pong`    | `ping` 응답 (서버 선택적 전송)       | —                                                                                        |
|                | `unread`  | 방별 읽지 않은 메시지 개수 알림          | `{ room: number, count: number }`                                                        |
