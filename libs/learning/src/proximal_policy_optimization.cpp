#include "proximal_policy_optimization.h"

auto PPO::returns(VT& rewards, VT& dones, VT& vals, double gamma, double lambda) -> VT
{
    // Compute the returns.
    torch::Tensor gae = torch::zeros({1}, rewards[0].options());
    VT returns(rewards.size(), torch::zeros({1}, rewards[0].options()));

    for (uint i=rewards.size();i-- >0;) // inverse for loops over unsigned: https://stackoverflow.com/questions/665745/whats-the-best-way-to-do-a-reverse-for-loop-with-an-unsigned-index/665773
    {
        // Advantage.
        auto delta = rewards[i] + gamma*vals[i+1]*(1-dones[i]) - vals[i];
        gae = delta + gamma*lambda*(1-dones[i])*gae;

        returns[i] = gae + vals[i];
    }

    return returns;
}

auto PPO::update(ActorCriticNMPC& ac,
                 torch::Tensor& states_pos,
                 torch::Tensor& states_map,
                 torch::Tensor& actions,
                 torch::Tensor& log_probs,
                 torch::Tensor& returns,
                 torch::Tensor& advantages, 
                 OPT& opt, 
                 uint steps, uint epochs, uint mini_batch_size, double beta, double clip_param) -> void
{
    for (uint e=0;e<epochs;e++)
    {
        // Generate random indices.
        torch::Tensor cpy_pos = torch::zeros({mini_batch_size, states_pos.size(1)}, states_pos.type());
        torch::Tensor cpy_map = torch::zeros({mini_batch_size, states_map.size(1), states_map.size(2), states_map.size(3)}, states_map.type());
        torch::Tensor cpy_act = torch::zeros({mini_batch_size, actions.size(1)}, actions.type());
        torch::Tensor cpy_log = torch::zeros({mini_batch_size, log_probs.size(1)}, log_probs.type());
        torch::Tensor cpy_ret = torch::zeros({mini_batch_size, returns.size(1)}, returns.type());
        torch::Tensor cpy_adv = torch::zeros({mini_batch_size, advantages.size(1)}, advantages.type());

        for (uint b=0;b<mini_batch_size;b++) {

            uint idx = std::uniform_int_distribution<uint>(0, steps-1)(re);
            cpy_pos[b] = states_pos[idx];
            cpy_map[b] = states_map[idx];
            cpy_act[b] = actions[idx];
            cpy_log[b] = log_probs[idx];
            cpy_ret[b] = returns[idx];
            cpy_adv[b] = advantages[idx];
        }

        auto av = ac->forward(cpy_pos, cpy_map); // action value pairs
        auto action = std::get<0>(av);
        auto entropy = ac->entropy();
        auto new_log_prob = ac->log_prob(cpy_act);

        auto old_log_prob = cpy_log;
        auto ratio = (new_log_prob - old_log_prob).exp();
        auto surr1 = ratio*cpy_adv;
        auto surr2 = torch::clamp(ratio, 1. - clip_param, 1. + clip_param)*cpy_adv;

        auto val = std::get<1>(av);
        auto actor_loss = -torch::min(surr1, surr2);
        auto critic_loss = (cpy_ret-val).pow(2);

        auto loss = 0.5*critic_loss+actor_loss-beta*entropy;

        opt.zero_grad();
        loss.backward();
        opt.step();
    }
}

auto PPO::update(ActorCritic& ac,
                 torch::Tensor& states,
                 torch::Tensor& actions,
                 torch::Tensor& log_probs,
                 torch::Tensor& returns,
                 torch::Tensor& advantages, 
                 OPT& opt, 
                 uint steps, uint epochs, uint mini_batch_size, double beta, double clip_param) -> void
{
    for (uint e=0;e<epochs;e++)
    {
        // Generate random indices.
        torch::Tensor cpy_sta = torch::zeros({mini_batch_size, states.size(1)}, states.type());
        torch::Tensor cpy_act = torch::zeros({mini_batch_size, actions.size(1)}, actions.type());
        torch::Tensor cpy_log = torch::zeros({mini_batch_size, log_probs.size(1)}, log_probs.type());
        torch::Tensor cpy_ret = torch::zeros({mini_batch_size, returns.size(1)}, returns.type());
        torch::Tensor cpy_adv = torch::zeros({mini_batch_size, advantages.size(1)}, advantages.type());

        for (uint b=0;b<mini_batch_size;b++) {

            uint idx = std::uniform_int_distribution<uint>(0, steps-1)(re);
            cpy_sta[b] = states[idx];
            cpy_act[b] = actions[idx];
            cpy_log[b] = log_probs[idx];
            cpy_ret[b] = returns[idx];
            cpy_adv[b] = advantages[idx];
        }

        auto av = ac->forward(cpy_sta); // action value pairs
        auto action = std::get<0>(av);
        auto entropy = ac->entropy();
        auto new_log_prob = ac->log_prob(cpy_act);

        auto old_log_prob = cpy_log;
        auto ratio = (new_log_prob - old_log_prob).exp();
        auto surr1 = ratio*cpy_adv;
        auto surr2 = torch::clamp(ratio, 1. - clip_param, 1. + clip_param)*cpy_adv;

        auto val = std::get<1>(av);
        auto actor_loss = -torch::min(surr1, surr2);
        auto critic_loss = (cpy_ret-val).pow(2);

        auto loss = 0.5*critic_loss+actor_loss-beta*entropy;

        opt.zero_grad();
        loss.backward();
        opt.step();
    }
}
