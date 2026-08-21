#pragma once

#include <string>
#include <vector>

#include "io/PhotonSource.hpp"


struct HitCsvConfig
{
    std::string csv_path;
};


class HitCsvSource : public PhotonSource
{

public:

    explicit HitCsvSource(
        const HitCsvConfig& cfg
    );


    bool next(
        PhotonBunch& out
    ) override;


    void reset() override;


    std::size_t size() const
    {
        return rows_.size();
    }


private:

    HitCsvConfig cfg_;

    std::size_t index_ = 0;


    std::vector<PhotonBunch> rows_;


    void load();

};
