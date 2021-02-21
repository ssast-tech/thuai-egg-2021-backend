#include "world.h"
#include "entities_json.h"
#include <cmath>
#include <exception>
#include <random>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace thuai {
double get_walk_speed_with_egg(double egg_score) {
  return 3 - pow(1.07, egg_score - 10);
}

World::World() {
  const double pi = acos(-1);
  {
    const double angle_delta = 2 * pi / 15, player_radius = DIAMETER / 4;
    double angle = .0;
    for (int i = 0; i < PLAYER_COUNT; i++) {
      if (i % 4 == 0)
        angle += angle_delta;
      players[i] = new Player(i);
      players[i]->set_position(
          {player_radius * cos(angle), player_radius * sin(angle)});
      angle += angle_delta;
    }
  }
  {
    std::random_device rd;
    std::mt19937 mtgen(rd());
    int egg_id = 0;
    for (double angle = 0; angle < 1.95 * pi; angle += 2 * pi / 3) {
      const double egg_radius_delta = DIAMETER / 12;
      for (int k = 1; k <= 5; k++, egg_id++) {
        double egg_radius = egg_radius_delta * k;
        int score =
            int(mtgen() * 10.0 / std::mt19937::max() + 10.0); // score: [10, 20)
        eggs[egg_id] = new Egg(egg_id, score);
        eggs[egg_id]->set_position(
            {egg_radius * cos(angle), egg_radius * sin(angle)});
      }
    }
  }
  // ========== Box2d Related ==========
  {
    // Initialize b2World
    b2world = new b2World(b2Vec2(0, 0));
    b2BodyDef groundBodyDef;
    groundBodyDef.position.Set(0.0f, 0.0f);
    b2Body *groundBody = b2world->CreateBody(&groundBodyDef);

    b2CircleShape groundCircle;
    groundCircle.m_radius = (DIAMETER / 2);
    b2PolygonShape goalBox[3];
    for (auto &item:goalBox)
        item.SetAsBox(GOAL_LENGTH, GOAL_WIDTH);
    // TODO: finish the goal region

    groundBody->CreateFixture(&groundCircle, 0.0f);
  }
  {
    // Initialize Players & eggs
    for (int i = 0; i < PLAYER_COUNT; i++) {
      b2BodyDef bodyDef;
      bodyDef.type = b2_dynamicBody;
      bodyDef.position.Set(float(players[i]->position().x), float(players[i]->position().y));
      b2players[i] = b2world->CreateBody(&bodyDef);
      b2CircleShape dynamicBox;
      dynamicBox.m_radius = .24f;
      b2FixtureDef fixtureDef;
      fixtureDef.shape = &dynamicBox;
      fixtureDef.friction = 0;
      b2players[i]->CreateFixture(&fixtureDef);
      b2MassData massdata;
      massdata.mass = 50;
      b2players[i]->SetMassData(&massdata);
    }
    for (int i = 0; i < EGG_COUNT; i++) {
      b2BodyDef bodyDef;
      bodyDef.type = b2_dynamicBody;
      bodyDef.position.Set(float(eggs[i]->position().x), float(eggs[i]->position().y));
      b2eggs[i] = b2world->CreateBody(&bodyDef);
      b2CircleShape dynamicBox;
      dynamicBox.m_radius = .35f;
      b2FixtureDef fixtureDef;
      fixtureDef.shape = &dynamicBox;
      fixtureDef.friction = 0;
      b2eggs[i]->CreateFixture(&fixtureDef);
      b2MassData massdata;
      massdata.mass = 30;
      b2eggs[i]->SetMassData(&massdata);
    }
  }
}

World::~World() {
  for (int i = 0; i < PLAYER_COUNT; i++)
    delete players[i];
  for (int i = 0; i < EGG_COUNT; i++)
    delete eggs[i];
  delete b2world;
}

void World::read_from_team_action(Team team, nlohmann::json detail) {
  for (int i = 0; i < 4; i++) {
    auto player_action = detail["actions"][i];
    int player_id = i + int(team) * 4;
    auto current_player = players[player_id];
    if (current_player->status() == SLIPPED) {
      continue; // when slipped, a player cannot do anything
    }

    //--------- handle movement ---------
    if (player_action["action"] == "running" &&
        (players[player_id]->endurance() <= 0 ||
         players[player_id]->egg() != -1)) {
      player_action["action"] = "walking"; // unify all cases of walking
    }

    Vec2D facing;
    player_action["facing"].get_to(facing);
    facing = facing.normalized();
    auto new_status = player_action["action"].get<PlayerStatus>();
    if (new_status != SLIPPED) {
      current_player->set_status(new_status);
    }
    //--------- handle egg placement ---------
    if (!player_action["drop"].is_null()) {
      const double radian = player_action["drop"];
      // TODO: resolve if two eggs to be placed at conflicted posistion

    }
    if (!player_action["grab"].is_null()) {
      const double egg_target = player_action["grab"];
      // TODO: make sure only the nearest player grab the egg
    }
  }
}

bool thuai::World::Update(int FPS, int32 velocityIterations,
                          int32 positionIterations) {
  try {
    static const float timestep = 1.0f / float(FPS);


    for (int i = 0; i < PLAYER_COUNT; i++) {
      auto currentPlayer = players[i];
      auto b2currentPlayer = b2players[i];
      if (currentPlayer->status() == PlayerStatus::RUNNING)
        if (currentPlayer->endurance() > 4.0 / float(FPS)) {
          currentPlayer->set_endurance(currentPlayer->endurance() -
                                       4.0 / float(FPS));
          b2currentPlayer->SetLinearVelocity(
            { float(currentPlayer->facing().x * RUN_SPEED),
              float(currentPlayer->facing().y * RUN_SPEED) });

        } else
          currentPlayer->set_status(PlayerStatus::WALKING);
      if (currentPlayer->status() == PlayerStatus::WALKING) {
        float newendurance = currentPlayer->endurance() + 0.5 / float(FPS);
        currentPlayer->set_endurance(newendurance > 5 ? 5 : newendurance);
        int egg_holding = currentPlayer->egg();
        double walk_speed = WALK_SPEED_EMPTY;
        if (egg_holding != -1) {
          walk_speed = get_walk_speed_with_egg(eggs[egg_holding]->score());
        }
        b2currentPlayer->SetLinearVelocity(
          { float(currentPlayer->facing().x * walk_speed),
            float(currentPlayer->facing().y * walk_speed) });
      }
      if (currentPlayer->status() == PlayerStatus::STOPPED) {
        float newendurance = currentPlayer->endurance() + 1 / float(FPS);
        currentPlayer->set_endurance(newendurance > 5 ? 5 : newendurance);
        b2currentPlayer->SetLinearVelocity({ .0, .0 });
      }

    }


    b2world->Step(timestep,
                  velocityIterations,
                  positionIterations); // do the simulation

    for (int i = 0; i < PLAYER_COUNT; i++) {
        players[i]->set_position(
          { b2players[i]->GetPosition().x, b2players[i]->GetPosition().y });
        players[i]->set_velocity({ b2players[i]->GetLinearVelocity().x,
                                   b2players[i]->GetLinearVelocity().y });
    }

    for (int i = 0; i < EGG_COUNT; i++) {
      eggs[i]->set_position(
          {b2eggs[i]->GetPosition().x, b2eggs[i]->GetPosition().y});
      eggs[i]->set_velocity(
          {b2eggs[i]->GetLinearVelocity().x, b2eggs[i]->GetLinearVelocity().y});
    }
  } catch (std::exception &e) {
    return false;
  }
  return true; 
}

nlohmann::json World::output_to_ai(int state) const {
  using json = nlohmann::json;
  json ret;
  ret["state"] = state;
  ret["eggs"] = json::array();
  ret["teams"] = {json::array(), json::array(), json::array()};
  for (int i = 0; i < EGG_COUNT; i++) {
    ret["eggs"][i] = {
        {"position", eggs[i]->position()},
        {"holder", -1} // set this later
    };
  }
  for (int i = 0; i < PLAYER_COUNT; i++) {
    int team = players[i]->team(), sub_id = i % 4;
    auto facing = players[i]->velocity().normalized();
    int holding = players[i]->egg();
    ret["eggs"][holding]["holder"] = i;
    ret["teams"][team][sub_id] = {
        {"position", players[i]->position()},
        {"status", players[i]->status()},
        {"facing", facing}};
  }
  return ret;
}
} // namespace thuai