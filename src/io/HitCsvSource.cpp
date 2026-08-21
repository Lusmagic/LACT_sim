#include "io/HitCsvSource.hpp"

#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>
#include <cctype>
#include <stdexcept>



namespace
{


std::string trim(
    const std::string& s
)
{
    auto first =
        std::find_if_not(
            s.begin(),
            s.end(),
            [](unsigned char c)
            {
                return std::isspace(c);
            });


    auto last =
        std::find_if_not(
            s.rbegin(),
            s.rend(),
            [](unsigned char c)
            {
                return std::isspace(c);
            }).base();


    if(first>=last)
        return "";


    return std::string(first,last);
}



std::vector<std::string>
split(
    const std::string& line
)
{

    std::stringstream ss(line);

    std::string cell;

    std::vector<std::string> out;


    while(
        std::getline(ss,cell,',')
    )
    {
        out.push_back(trim(cell));
    }


    return out;
}



std::string lower(
    std::string s
)
{
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](unsigned char c)
        {
            return std::tolower(c);
        });

    return s;
}



int column(
    const std::map<std::string,int>& header,
    const std::string& name
)
{
    auto it =
        header.find(
            lower(name)
        );


    if(it==header.end())
        return -1;


    return it->second;
}



double getDouble(
    const std::vector<std::string>& cells,
    const std::map<std::string,int>& header,
    const std::string& name,
    double default_value=0
)
{

    int id =
        column(header,name);


    if(id<0 ||
       id>=static_cast<int>(cells.size()))
        return default_value;


    return std::stod(
        cells[id]
    );
}


}



HitCsvSource::HitCsvSource(
    const HitCsvConfig& cfg
)
:
cfg_(cfg)
{

    load();

}



void HitCsvSource::reset()
{
    index_=0;
}



bool HitCsvSource::next(
    PhotonBunch& out
)
{

    if(index_>=rows_.size())
        return false;


    out =
        rows_[index_++];


    return true;

}



void HitCsvSource::load()
{

    std::ifstream file(
        cfg_.csv_path
    );


    if(!file)
    {
        throw std::runtime_error(
            "cannot open hits csv: "
            +cfg_.csv_path
        );
    }



    std::string line;


    if(!std::getline(file,line))
    {
        throw std::runtime_error(
            "empty hits csv"
        );
    }



    auto names =
        split(line);



    std::map<std::string,int> header;


    for(int i=0;i<(int)names.size();i++)
    {
        header[
            lower(names[i])
        ]=i;
    }



    while(
        std::getline(file,line)
    )
    {

        if(trim(line).empty())
            continue;


        auto cells =
            split(line);



        PhotonBunch bunch;



        /*
          mirror reflected point

          surface_x/y/z
          是白板位置
        */


        bunch.photon.pos =
        {
            getDouble(
                cells,
                header,
                "surface_x"
            ),

            getDouble(
                cells,
                header,
                "surface_y"
            ),

            getDouble(
                cells,
                header,
                "surface_z"
            )
        };



        bunch.photon.dir =
        {
            getDouble(
                cells,
                header,
                "dir_x"
            ),

            getDouble(
                cells,
                header,
                "dir_y"
            ),

            getDouble(
                cells,
                header,
                "dir_z"
            )
        };



        bunch.photon.wavelength_nm =
            getDouble(
                cells,
                header,
                "wavelength_nm",
                400
            );



        bunch.photon.time_ns =
            getDouble(
                cells,
                header,
                "time_ns",
                0
            );



        bunch.photon.weight =
            getDouble(
                cells,
                header,
                "weight",
                1
            );



        bunch.multiplicity = 1;



        rows_.push_back(
            bunch
        );

    }


    if(rows_.empty())
    {
        throw std::runtime_error(
            "hits csv has no photons"
        );
    }

}
